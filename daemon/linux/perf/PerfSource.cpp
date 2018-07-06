/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "linux/perf/PerfSource.h"

#include <algorithm>
#include <cstring>
#include <cinttypes>

#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "Child.h"
#include "DynBuf.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "linux/perf/PerfDriver.h"
#include "Proc.h"
#include "SessionData.h"
#include "lib/Time.h"

#ifndef SCHED_RESET_ON_FORK
#define SCHED_RESET_ON_FORK 0x40000000
#endif

static void *syncFunc(void *arg)
{
    struct timespec ts;
    int64_t nextTime = gSessionData.mMonotonicStarted;
    int err;
    (void) arg;

    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-sync"), 0, 0, 0);

    // Mask all signals so that this thread will not be woken up
    {
        sigset_t set;
        if (sigfillset(&set) != 0) {
            logg.logError("sigfillset failed");
            handleException();
        }
        if ((err = pthread_sigmask(SIG_SETMASK, &set, NULL)) != 0) {
            logg.logError("pthread_sigmask failed");
            handleException();
        }
    }

    for (;;) {
        if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
            logg.logError("clock_gettime failed");
            handleException();
        }
        const int64_t currTime = ts.tv_sec * NS_PER_S + ts.tv_nsec;

        // Wake up once a second
        nextTime += NS_PER_S;

        // Always sleep more than 1 ms, hopefully things will line up better next time
        const int64_t sleepTime = std::max < int64_t > (nextTime - currTime, NS_PER_MS + 1);
        ts.tv_sec = sleepTime / NS_PER_S;
        ts.tv_nsec = sleepTime % NS_PER_S;

        err = nanosleep(&ts, NULL);
        if (err != 0) {
            fprintf(stderr, "clock_nanosleep failed: %s\n", strerror(err));
            return NULL;
        }
    }

    return NULL;
}

PerfSource::PerfSource(PerfDriver & driver, Child & child, sem_t & senderSem, sem_t & startProfile, const std::set<int> & appTids)
        : Source(child),
          mDriver(driver),
          mSummary(0, FRAME_SUMMARY, 1024, &senderSem),
          mBuffer(NULL),
          mCountersBuf(),
          mCountersGroup(&mCountersBuf, driver.getConfig()),
          mMonitor(),
          mUEvent(),
          mSenderSem(senderSem),
          mStartProfile(startProfile),
          mInterruptFd(-1),
          mIsDone(false),
          mAppTids(appTids)
{
}

PerfSource::~PerfSource()
{
    delete mBuffer;
}

bool PerfSource::prepare()
{
    DynBuf printb;
    DynBuf b1;

    const PerfConfig & mConfig = mDriver.getConfig();

    // MonotonicStarted has not yet been assigned!
    const uint64_t currTime = 0; //getTime() - gSessionData.mMonotonicStarted;

    mBuffer = new Buffer(0, FRAME_PERF_ATTRS, gSessionData.mTotalBufferSize * 1024 * 1024, &mSenderSem);

    // Reread cpuinfo since cores may have changed since startup
    gSessionData.readCpuInfo();

    if (!mMonitor.init()) {
        logg.logMessage("monitor setup failed");
        return false;
    }

    if (mConfig.is_system_wide && (!mUEvent.init() || !mMonitor.add(mUEvent.getFd())))
    {
        logg.logMessage("uevent setup failed");
        return false;
    }

    if (mConfig.can_access_tracepoints && !mDriver.sendTracepointFormats(currTime, mBuffer, &printb, &b1)) {
        logg.logMessage("could not send tracepoint formats");
        return false;
    }

    if (!mDriver.enable(currTime, &mCountersGroup, mBuffer)) {
        logg.logMessage("perf setup failed, are you running Linux 3.4 or later?");
        return false;
    }

    int numOnlined = 0;

    for (int cpu = 0; cpu < gSessionData.mCores; ++cpu) {
        using Result = OnlineResult;
        const Result result = mCountersGroup.onlineCPU(currTime, cpu, mAppTids, false, mBuffer, &mMonitor);
        switch (result) {
        case Result::FAILURE:
            logg.logError("PerfGroups::prepareCPU on mCountersGroup failed");
            handleException();
            break;
        case Result::SUCCESS:
            numOnlined++;
            break;
        default:
            // do nothing
            // why distinguish between FAILURE and OTHER_FAILURE?
            break;
        }
    }

    if (numOnlined <= 0) {
        logg.logMessage("PerfGroups::onlineCPU failed on all cores");
    }

    // Send the summary right before the start so that the monotonic delta is close to the start time
    if (!mDriver.summary(&mSummary)) {
        logg.logError("PerfDriver::summary failed");
        handleException();
    }

    if (!mConfig.has_attr_clockid_support) {
        // Start the timer thread to used to sync perf and monotonic raw times
        pthread_t syncThread;
        if (pthread_create(&syncThread, NULL, syncFunc, NULL)) {
            logg.logError("pthread_create failed");
            handleException();
        }
        struct sched_param param;
        param.sched_priority = sched_get_priority_max(SCHED_FIFO);
        if (pthread_setschedparam(syncThread, SCHED_FIFO | SCHED_RESET_ON_FORK, &param) != 0) {
            logg.logMessage("Unable to schedule sync thread as FIFO, trying OTHER");
            param.sched_priority = sched_get_priority_max(SCHED_OTHER);
            if (pthread_setschedparam(syncThread, SCHED_OTHER | SCHED_RESET_ON_FORK, &param) != 0) {
                logg.logError("pthread_setschedparam failed");
                handleException();
            }
        }
    }

    mBuffer->commit(currTime);

    return true;
}

struct ProcThreadArgs
{
    Buffer *mBuffer;
    uint64_t mCurrTime;
    bool mIsDone;
};

static void *procFunc(void *arg)
{
    DynBuf printb;
    DynBuf b;
    const ProcThreadArgs * const args = reinterpret_cast<const ProcThreadArgs *>(arg);

    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-proc"), 0, 0, 0);

    // Gator runs at a high priority, reset the priority to the default
    if (setpriority(PRIO_PROCESS, syscall(__NR_gettid), 0) == -1) {
        logg.logError("setpriority failed");
        handleException();
    }

    if (!readProcMaps(args->mCurrTime, *args->mBuffer)) {
        logg.logError("readProcMaps failed");
        handleException();
    }

    if (!readKallsyms(args->mCurrTime, args->mBuffer, &args->mIsDone)) {
        logg.logError("readKallsyms failed");
        handleException();
    }
    args->mBuffer->commit(args->mCurrTime);

    return NULL;
}

static const char CPU_DEVPATH[] = "/devices/system/cpu/cpu";

void PerfSource::run()
{
    int pipefd[2];
    pthread_t procThread;
    ProcThreadArgs procThreadArgs;

    if (pipe_cloexec(pipefd) != 0) {
        logg.logError("pipe failed");
        handleException();
    }
    mInterruptFd = pipefd[1];

    if (!mMonitor.add(pipefd[0])) {
        logg.logError("Monitor::add failed");
        handleException();
    }

    {
        DynBuf printb;
        DynBuf b1;

        const uint64_t currTime = getTime() - gSessionData.mMonotonicStarted;
        logg.logMessage("run at current time: %" PRIu64, currTime);

        // Start events before reading proc to avoid race conditions
        mCountersGroup.start();

        mBuffer->perfCounterHeader(currTime);
        for (int cpu = 0; cpu < gSessionData.mCores; ++cpu) {
            mDriver.read(mBuffer, cpu);
        }
        mBuffer->perfCounterFooter(currTime);

        if (!readProcSysDependencies(currTime, *mBuffer, &printb, &b1)) {
            if (mDriver.getConfig().is_system_wide) {
                logg.logError("readProcSysDependencies failed");
                handleException();
            }
            else {
                logg.logMessage("readProcSysDependencies failed");
            }
        }
        mBuffer->commit(currTime);

        // Postpone reading kallsyms as on android adb gets too backed up and data is lost
        procThreadArgs.mBuffer = mBuffer;
        procThreadArgs.mCurrTime = currTime;
        procThreadArgs.mIsDone = false;
        if (pthread_create(&procThread, NULL, procFunc, &procThreadArgs)) {
            logg.logError("pthread_create failed");
            handleException();
        }
    }

    sem_post(&mStartProfile);

    const uint64_t NO_RATE = ~0ULL;
    const uint64_t rate = gSessionData.mLiveRate > 0 && gSessionData.mSampleRate > 0 ? gSessionData.mLiveRate : NO_RATE;
    uint64_t nextTime = 0;
    int timeout = rate != NO_RATE ? 0 : -1;
    while (gSessionData.mSessionIsActive) {
        // +1 for uevents, +1 for pipe
        struct epoll_event events[NR_CPUS + 2];
        int ready = mMonitor.wait(events, ARRAY_LENGTH(events), timeout);
        if (ready < 0) {
            logg.logError("Monitor::wait failed");
            handleException();
        }
        const uint64_t currTime = getTime() - gSessionData.mMonotonicStarted;

        for (int i = 0; i < ready; ++i) {
            if (events[i].data.fd == mUEvent.getFd()) {
                if (!handleUEvent(currTime)) {
                    logg.logError("PerfSource::handleUEvent failed");
                    handleException();
                }
                break;
            }
        }

        // send a notification that data is ready
        sem_post(&mSenderSem);

        // In one shot mode, stop collection once all the buffers are filled
        if (gSessionData.mOneShot && gSessionData.mSessionIsActive
                && ((mSummary.bytesAvailable() <= 0) || (mBuffer->bytesAvailable() <= 0) || mCountersBuf.isFull())) {
            logg.logMessage("One shot (perf)");
            mChild.endSession();
        }

        if (rate != NO_RATE) {
            while (currTime > nextTime) {
                nextTime += rate;
            }
            // + NS_PER_MS - 1 to ensure always rounding up
            timeout = std::max<int>(
                    0, (nextTime + NS_PER_MS - 1 - getTime() + gSessionData.mMonotonicStarted) / NS_PER_MS);
        }
    }

    procThreadArgs.mIsDone = true;
    pthread_join(procThread, NULL);
    mCountersGroup.stop();
    mBuffer->setDone();
    mIsDone = true;

    // send a notification that data is ready
    sem_post(&mSenderSem);

    mInterruptFd = -1;
    close(pipefd[0]);
    close(pipefd[1]);
}

bool PerfSource::handleUEvent(const uint64_t currTime)
{
    UEventResult result;
    if (!mUEvent.read(&result)) {
        logg.logMessage("UEvent::Read failed");
        return false;
    }

    if (strcmp(result.mSubsystem, "cpu") == 0) {
        if (strncmp(result.mDevPath, CPU_DEVPATH, sizeof(CPU_DEVPATH) - 1) != 0) {
            logg.logMessage("Unexpected cpu DEVPATH format");
            return false;
        }
        int cpu;
        if (!stringToInt(&cpu, result.mDevPath + sizeof(CPU_DEVPATH) - 1, 10)) {
            logg.logMessage("stringToInt failed");
            return false;
        }

        if (cpu >= gSessionData.mCores) {
            logg.logError("Only %i cores are expected but core %i reports %s", gSessionData.mCores, cpu, result.mAction);
            handleException();
        }

        if (strcmp(result.mAction, "online") == 0) {
            mBuffer->onlineCPU(currTime, cpu);
            using Result = OnlineResult;
            bool ret;
            switch (mCountersGroup.onlineCPU(currTime, cpu, mAppTids, true, mBuffer, &mMonitor)) {
            case Result::SUCCESS:
                mBuffer->perfCounterHeader(currTime);
                mDriver.read(mBuffer, cpu);
                mBuffer->perfCounterFooter(currTime);
                // fall through
                /* no break */
            case Result::CPU_OFFLINE:
                ret = true;
                break;
            default:
                ret = false;
                break;
            }
            mBuffer->commit(currTime);

            gSessionData.readCpuInfo();
            mDriver.coreName(currTime, &mSummary, cpu);
            mSummary.commit(currTime);
            return ret;
        }
        else if (strcmp(result.mAction, "offline") == 0) {
            const bool ret = mCountersGroup.offlineCPU(cpu);
            mBuffer->offlineCPU(currTime, cpu);
            return ret;
        }
    }

    return true;
}

void PerfSource::interrupt()
{
    if (mInterruptFd >= 0) {
        int8_t c = 0;
        // Write to the pipe to wake the monitor which will cause mSessionIsActive to be reread
        if (::write(mInterruptFd, &c, sizeof(c)) != sizeof(c)) {
            logg.logError("write failed");
            handleException();
        }
    }
}

bool PerfSource::isDone()
{
    return mBuffer->isDone() && mIsDone && mCountersBuf.isEmpty();
}

void PerfSource::write(Sender *sender)
{
    if (!mSummary.isDone()) {
        mSummary.write(sender);
        gSessionData.mSentSummary = true;
    }
    if (!mBuffer->isDone()) {
        mBuffer->write(sender);
    }
    if (!mCountersBuf.send(sender)) {
        logg.logError("PerfBuffer::send failed");
        handleException();
    }
}
