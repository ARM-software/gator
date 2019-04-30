/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "linux/perf/PerfSource.h"
#include "linux/perf/PerfSyncThreadBuffer.h"

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
#include "ICpuInfo.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "linux/perf/PerfAttrsBuffer.h"
#include "linux/perf/PerfDriver.h"
#include "linux/perf/PerfCpuOnlineMonitor.h"
#include "linux/proc/ProcessChildren.h"
#include "Proc.h"
#include "Protocol.h"
#include "Sender.h"
#include "SessionData.h"
#include "lib/Time.h"
#include "lib/FileDescriptor.h"

#ifndef SCHED_RESET_ON_FORK
#define SCHED_RESET_ON_FORK 0x40000000
#endif

static PerfBuffer::Config createPerfBufferConfig()
{
    return {static_cast<size_t>(gSessionData.mPageSize),
        static_cast<size_t>(gSessionData.mPerfMmapSizeInPages > 0 ? gSessionData.mPageSize * gSessionData.mPerfMmapSizeInPages :
                gSessionData.mTotalBufferSize * 1024 * 1024)};
}

PerfSource::PerfSource(PerfDriver & driver, Child & child, sem_t & senderSem, sem_t & startProfile,
                       const std::set<int> & appTids, FtraceDriver & ftraceDriver, bool enableOnCommandExec, ICpuInfo & cpuInfo)
        : Source(child),
          mSummary(1024 * 1024, &senderSem),
          mCountersBuf(createPerfBufferConfig()),
          mCountersGroup(driver.getConfig(), mCountersBuf.calculateBufferLength(), gSessionData.mBacktraceDepth,
                         gSessionData.mSampleRate, gSessionData.mIsEBS, cpuInfo.getClusters(),
                         cpuInfo.getClusterIds(), getTracepointId(SCHED_SWITCH)),
          mMonitor(),
          mUEvent(),
          mAppTids(appTids),
          mDriver(driver),
          mAttrsBuffer(NULL),
          mSenderSem(senderSem),
          mStartProfile(startProfile),
          mInterruptFd(-1),
          mIsDone(false),
          mFtraceDriver(ftraceDriver),
          mCpuInfo(cpuInfo),
          mSyncThreads(),
          enableOnCommandExec(false)
{
    const PerfConfig & mConfig = mDriver.getConfig();

    if ((!mConfig.is_system_wide) && (!mConfig.has_attr_clockid_support)) {
        logg.logMessage("Tracing gatord as well as target application as no clock_id support");
        mAppTids.insert(getpid());
    }

    // was !enableOnCommandExec but this causes us to miss the exec comm record associated with the
    this->enableOnCommandExec = (enableOnCommandExec && mConfig.has_attr_clockid_support && mConfig.has_attr_comm_exec);
}

PerfSource::~PerfSource()
{
    delete mAttrsBuffer;
}

bool PerfSource::prepare()
{
    const PerfConfig & mConfig = mDriver.getConfig();

    // MonotonicStarted has not yet been assigned!
    const uint64_t currTime = 0; //getTime() - gSessionData.mMonotonicStarted;

    mAttrsBuffer = new PerfAttrsBuffer(gSessionData.mTotalBufferSize * 1024 * 1024, &mSenderSem);

    // Reread cpuinfo since cores may have changed since startup
    mCpuInfo.updateIds(false);

    if (!mMonitor.init()) {
        logg.logMessage("monitor setup failed");
        return false;
    }

    if (mConfig.is_system_wide && (!mUEvent.init() || !mMonitor.add(mUEvent.getFd())))
    {
        logg.logMessage("uevent setup failed");
        return false;
    }

    if (mConfig.can_access_tracepoints && !mDriver.sendTracepointFormats(currTime, *mAttrsBuffer)) {
        logg.logMessage("could not send tracepoint formats");
        return false;
    }

    if (!mDriver.enable(currTime, mCountersGroup, *mAttrsBuffer)) {
        logg.logMessage("perf setup failed, are you running Linux 3.4 or later?");
        return false;
    }

    // online them later
    const OnlineEnabledState onlineEnabledState = (enableOnCommandExec ? OnlineEnabledState::ENABLE_ON_EXEC : OnlineEnabledState::NOT_ENABLED);
    int numOnlined = 0;

    for (size_t cpu = 0; cpu < mCpuInfo.getNumberOfCores(); ++cpu) {
        using Result = OnlineResult;
        const Result result = mCountersGroup.onlineCPU(
                currTime, cpu, mAppTids, onlineEnabledState,
                *mAttrsBuffer, //
                [this](int fd) -> bool {return mMonitor.add(fd);},
                [this](int fd, int cpu, bool hasAux) -> bool {return mCountersBuf.useFd(fd, cpu, hasAux);},
                &lnx::getChildTids);
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
    if (!mDriver.summary(mSummary, []() -> uint64_t {return (gSessionData.mMonotonicStarted = getTime());})) {
        logg.logError("PerfDriver::summary failed");
        handleException();
    }

    mAttrsBuffer->commit(currTime);

    return true;
}

struct ProcThreadArgs
{
    PerfAttrsBuffer *mAttrsBuffer {nullptr};
    uint64_t mCurrTime {0};
    std::atomic_bool mIsDone {false};
};

static void *procFunc(void *arg)
{
    const ProcThreadArgs * const args = reinterpret_cast<const ProcThreadArgs *>(arg);

    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-proc"), 0, 0, 0);

    // Gator runs at a high priority, reset the priority to the default
    if (setpriority(PRIO_PROCESS, syscall(__NR_gettid), 0) == -1) {
        logg.logError("setpriority failed");
        handleException();
    }

    if (!readProcMaps(args->mCurrTime, *args->mAttrsBuffer)) {
        logg.logError("readProcMaps failed");
        handleException();
    }

    if (!readKallsyms(args->mCurrTime, *args->mAttrsBuffer, args->mIsDone)) {
        logg.logError("readKallsyms failed");
        handleException();
    }
    args->mAttrsBuffer->commit(args->mCurrTime);

    return NULL;
}

static const char CPU_DEVPATH[] = "/devices/system/cpu/cpu";

void PerfSource::run()
{
    int pipefd[2];
    pthread_t procThread;
    ProcThreadArgs procThreadArgs;

    if (lib::pipe_cloexec(pipefd) != 0) {
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
        if (!enableOnCommandExec) {
            mCountersGroup.start();
        }

        mAttrsBuffer->perfCounterHeader(currTime);
        for (size_t cpu = 0; cpu < mCpuInfo.getNumberOfCores(); ++cpu) {
            mDriver.read(*mAttrsBuffer, cpu);
        }
        mAttrsBuffer->perfCounterFooter(currTime);

        if (!readProcSysDependencies(currTime, *mAttrsBuffer, &printb, &b1, mFtraceDriver)) {
            if (mDriver.getConfig().is_system_wide) {
                logg.logError("readProcSysDependencies failed");
                handleException();
            }
            else {
                logg.logMessage("readProcSysDependencies failed");
            }
        }
        mAttrsBuffer->commit(currTime);


        // Postpone reading kallsyms as on android adb gets too backed up and data is lost
        procThreadArgs.mAttrsBuffer = mAttrsBuffer;
        procThreadArgs.mCurrTime = currTime;
        procThreadArgs.mIsDone = false;
        if (pthread_create(&procThread, NULL, procFunc, &procThreadArgs)) {
            logg.logError("pthread_create failed");
            handleException();
        }
    }

    // monitor online cores if no uevents
    std::unique_ptr<PerfCpuOnlineMonitor> onlineMonitorThread;
    if (!mUEvent.enabled()) {
        onlineMonitorThread.reset(new PerfCpuOnlineMonitor([&](unsigned cpu, bool online) -> void {
            logg.logMessage("CPU online state changed: %u -> %s", cpu, (online ? "online" : "offline"));
            const uint64_t currTime = getTime() - gSessionData.mMonotonicStarted;
            if (online) {
                handleCpuOnline(currTime, cpu);
            }
            else {
                handleCpuOffline(currTime, cpu);
            }
        }));
    }

    // start sync threads
    mSyncThreads = PerfSyncThreadBuffer::create(gSessionData.mMonotonicStarted, this->mDriver.getConfig().has_attr_clockid_support, this->mCountersGroup.hasSPE(), mSenderSem);

    // start profiling
    sem_post(&mStartProfile);

    const uint64_t NO_RATE = ~0ULL;
    const uint64_t rate = gSessionData.mLiveRate > 0 && gSessionData.mSampleRate > 0 ? gSessionData.mLiveRate : NO_RATE;
    uint64_t nextTime = 0;
    int timeout = rate != NO_RATE ? 0 : -1;
    while (gSessionData.mSessionIsActive) {
        // +1 for uevents, +1 for pipe
        std::vector<struct epoll_event> events {mCpuInfo.getNumberOfCores() + 2};
        int ready = mMonitor.wait(events.data(), events.size(), timeout);
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
                && ((mSummary.bytesAvailable() <= 0) || (mAttrsBuffer->bytesAvailable() <= 0) || mCountersBuf.isFull())) {
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

    if (onlineMonitorThread) {
        onlineMonitorThread->terminate();
    }

    procThreadArgs.mIsDone = true;
    pthread_join(procThread, NULL);
    mCountersGroup.stop();
    mAttrsBuffer->setDone();
    mIsDone = true;

    // terminate all remaining sync threads
    for (auto & ptr : mSyncThreads) {
        ptr->terminate();
    }

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

        if (static_cast<size_t>(cpu) >= mCpuInfo.getNumberOfCores()) {
            logg.logError("Only %zu cores are expected but core %i reports %s", mCpuInfo.getNumberOfCores(), cpu, result.mAction);
            handleException();
        }

        if (strcmp(result.mAction, "online") == 0) {
            return handleCpuOnline(currTime, cpu);
        }
        else if (strcmp(result.mAction, "offline") == 0) {
            return handleCpuOffline(currTime, cpu);
        }
    }

    return true;
}

bool PerfSource::handleCpuOnline(uint64_t currTime, unsigned cpu)
{
    mAttrsBuffer->onlineCPU(currTime, cpu);

    bool ret;
    const OnlineResult result = mCountersGroup.onlineCPU(
            currTime, cpu, mAppTids, OnlineEnabledState::ENABLE_NOW,
            *mAttrsBuffer, //
            [this](int fd) -> bool {return mMonitor.add(fd);},
            [this](int fd, int cpu, bool hasAux) -> bool {return mCountersBuf.useFd(fd, cpu, hasAux);},
            &lnx::getChildTids);

    switch (result) {
    case OnlineResult::SUCCESS:
        mAttrsBuffer->perfCounterHeader(currTime);
        mDriver.read(*mAttrsBuffer, cpu);
        mAttrsBuffer->perfCounterFooter(currTime);
        // fall through
        /* no break */
    case OnlineResult::CPU_OFFLINE:
        ret = true;
        break;
    default:
        ret = false;
        break;
    }

    mAttrsBuffer->commit(currTime);

    mCpuInfo.updateIds(true);
    mDriver.coreName(currTime, mSummary, cpu);
    mSummary.commit(currTime);
    return ret;
}

bool PerfSource::handleCpuOffline(uint64_t currTime, unsigned cpu)
{
    const bool ret = mCountersGroup.offlineCPU(cpu, [this] (int cpu) {mCountersBuf.discard(cpu);});
    mAttrsBuffer->offlineCPU(currTime, cpu);
    return ret;
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
    for (const auto & syncThread : mSyncThreads) {
        if (!syncThread->complete()) {
            return false;
        }
    }
    return mAttrsBuffer->isDone() && mIsDone && mCountersBuf.isEmpty();
}

void PerfSource::write(ISender *sender)
{
    if (!mSummary.isDone()) {
        mSummary.write(sender);
        gSessionData.mSentSummary = true;
    }
    if (!mAttrsBuffer->isDone()) {
        mAttrsBuffer->write(sender);
    }
    if (!mCountersBuf.send(*sender)) {
        logg.logError("PerfBuffer::send failed");
        handleException();
    }
    for (auto & syncThread : mSyncThreads) {
        if (!syncThread->complete()) {
            syncThread->send(*sender);
        }
    }
}
