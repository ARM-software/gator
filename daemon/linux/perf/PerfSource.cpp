/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfSource.h"

#include "Child.h"
#include "DynBuf.h"
#include "ICpuInfo.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "Proc.h"
#include "Protocol.h"
#include "Sender.h"
#include "SessionData.h"
#include "lib/FileDescriptor.h"
#include "lib/Time.h"
#include "linux/perf/PerfAttrsBuffer.h"
#include "linux/perf/PerfCpuOnlineMonitor.h"
#include "linux/perf/PerfDriver.h"
#include "linux/perf/PerfSyncThreadBuffer.h"
#include "linux/proc/ProcessChildren.h"

#include <algorithm>
#include <cinttypes>
#include <csignal>
#include <cstring>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

#ifndef SCHED_RESET_ON_FORK
#define SCHED_RESET_ON_FORK 0x40000000
#endif

static PerfBuffer::Config createPerfBufferConfig()
{
    return {
        static_cast<size_t>(gSessionData.mPageSize),
        static_cast<size_t>(gSessionData.mPerfMmapSizeInPages > 0
                                ? gSessionData.mPageSize * gSessionData.mPerfMmapSizeInPages
                                : gSessionData.mTotalBufferSize * 1024 * 1024),
        static_cast<size_t>(gSessionData.mPerfMmapSizeInPages > 0
                                ? gSessionData.mPageSize * gSessionData.mPerfMmapSizeInPages
                                : gSessionData.mTotalBufferSize * 1024 * 1024 * 64),
    };
}

PerfSource::PerfSource(PerfDriver & driver,
                       sem_t & senderSem,
                       std::function<void()> profilingStartedCallback,
                       std::set<int> appTids,
                       FtraceDriver & ftraceDriver,
                       bool enableOnCommandExec,
                       ICpuInfo & cpuInfo)
    : mSummary(1024 * 1024, senderSem),
      mCountersBuf(createPerfBufferConfig()),
      mCountersGroup(driver.getConfig(),
                     mCountersBuf.getDataBufferLength(),
                     mCountersBuf.getAuxBufferLength(),
                     // We disable periodic sampling if we have at least one EBS counter
                     // it should probably be independent of EBS though
                     gSessionData.mBacktraceDepth,
                     gSessionData.mSampleRate,
                     !gSessionData.mIsEBS,
                     cpuInfo.getClusters(),
                     cpuInfo.getClusterIds(),
                     getTracepointId(SCHED_SWITCH)),
      mMonitor(),
      mUEvent(),
      mAppTids(std::move(appTids)),
      mDriver(driver),
      mAttrsBuffer(),
      mProcBuffer(),
      mSenderSem(senderSem),
      mProfilingStartedCallback(std::move(profilingStartedCallback)),
      mIsDone(false),
      mFtraceDriver(ftraceDriver),
      mCpuInfo(cpuInfo),
      mSyncThread(),
      enableOnCommandExec(false)
{
    const PerfConfig & mConfig = mDriver.getConfig();

    if ((!mConfig.is_system_wide) && (!mConfig.has_attr_clockid_support)) {
        logg.logMessage("Tracing gatord as well as target application as no clock_id support");
        mAppTids.insert(getpid());
    }

    // was !enableOnCommandExec but this causes us to miss the exec comm record associated with the
    // enable on exec doesn't work for cpu-wide events.
    this->enableOnCommandExec = (enableOnCommandExec && !mConfig.is_system_wide && mConfig.has_attr_clockid_support &&
                                 mConfig.has_attr_comm_exec);
}

bool PerfSource::prepare()
{
    const PerfConfig & mConfig = mDriver.getConfig();

    mAttrsBuffer.reset(new PerfAttrsBuffer(gSessionData.mTotalBufferSize * 1024 * 1024, mSenderSem));
    mProcBuffer.reset(new PerfAttrsBuffer(gSessionData.mTotalBufferSize * 1024 * 1024, mSenderSem));

    // Reread cpuinfo since cores may have changed since startup
    mCpuInfo.updateIds(false);

    if (!mMonitor.init()) {
        logg.logMessage("monitor setup failed");
        return false;
    }

    int pipefd[2];
    if (lib::pipe_cloexec(pipefd) != 0) {
        logg.logError("pipe failed");
        return false;
    }
    mInterruptWrite = pipefd[1];
    mInterruptRead = pipefd[0];

    if (!mMonitor.add(*mInterruptRead)) {
        logg.logError("Monitor::add failed");
        return false;
    }

    if (mConfig.is_system_wide && (!mUEvent.init() || !mMonitor.add(mUEvent.getFd()))) {
        logg.logMessage("uevent setup failed");
        return false;
    }

    if (mConfig.can_access_tracepoints && !mDriver.sendTracepointFormats(*mAttrsBuffer)) {
        logg.logMessage("could not send tracepoint formats");
        return false;
    }

    if (!mDriver.enable(mCountersGroup, *mAttrsBuffer)) {
        logg.logMessage("perf setup failed, are you running Linux 3.4 or later?");
        return false;
    }

    // must do this after mDriver::enable because of the SPE check
    mSyncThread =
        PerfSyncThreadBuffer::create(mDriver.getConfig().has_attr_clockid_support, mCountersGroup.hasSPE(), mSenderSem);

    // online them later
    const OnlineEnabledState onlineEnabledState =
        (enableOnCommandExec ? OnlineEnabledState::ENABLE_ON_EXEC : OnlineEnabledState::NOT_ENABLED);
    int numOnlined = 0;

    for (size_t cpu = 0; cpu < mCpuInfo.getNumberOfCores(); ++cpu) {
        using Result = OnlineResult;
        const std::pair<OnlineResult, std::string> result = mCountersGroup.onlineCPU(
            cpu,
            mAppTids,
            onlineEnabledState,
            *mAttrsBuffer, //
            [this](int fd) -> bool { return mMonitor.add(fd); },
            [this](int fd, int cpu, bool hasAux) -> bool { return mCountersBuf.useFd(fd, cpu, hasAux); },
            &lnx::getChildTids);
        switch (result.first) {
            case Result::FAILURE:
                logg.logError("\n%s", result.second.c_str());
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

    mAttrsBuffer->flush();

    return true;
}

lib::Optional<uint64_t> PerfSource::sendSummary()
{
    // Send the summary right before the start so that the monotonic delta is close to the start time
    auto montonicStart = mDriver.summary(mSummary, &getTime);
    if (!montonicStart) {
        logg.logError("PerfDriver::summary failed");
        handleException();
    }

    return montonicStart;
}

struct ProcThreadArgs {
    PerfAttrsBuffer * mProcBuffer {nullptr};
    std::atomic_bool mIsDone {false};
};

static void * procFunc(void * arg)
{
    const auto * const args = reinterpret_cast<const ProcThreadArgs *>(arg);

    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-proc"), 0, 0, 0);

    // Gator runs at a high priority, reset the priority to the default
    if (setpriority(PRIO_PROCESS, syscall(__NR_gettid), 0) == -1) {
        logg.logError("setpriority failed");
        handleException();
    }

    if (!readProcMaps(*args->mProcBuffer)) {
        logg.logError("readProcMaps failed");
        handleException();
    }

    if (!readKallsyms(*args->mProcBuffer, args->mIsDone)) {
        logg.logError("readKallsyms failed");
        handleException();
    }
    args->mProcBuffer->flush();

    return nullptr;
}

static const char CPU_DEVPATH[] = "/devices/system/cpu/cpu";

void PerfSource::run(std::uint64_t monotonicStart, std::function<void()> endSession)
{
    pthread_t procThread;
    ProcThreadArgs procThreadArgs;

    {
        DynBuf printb;
        DynBuf b1;

        const uint64_t currTime = getTime() - monotonicStart;
        logg.logMessage("run at current time: %" PRIu64, currTime);

        // Start events before reading proc to avoid race conditions
        if (!enableOnCommandExec) {
            mCountersGroup.start();
        }

        // This a bit fragile, we are assuming the driver will only write one counter per CPU
        // which is true at the time of writing (just the cpu freq)
        mAttrsBuffer->perfCounterHeader(currTime, mCpuInfo.getNumberOfCores());
        for (size_t cpu = 0; cpu < mCpuInfo.getNumberOfCores(); ++cpu) {
            mDriver.read(*mAttrsBuffer, cpu);
        }
        mAttrsBuffer->perfCounterFooter();

        if (!readProcSysDependencies(*mAttrsBuffer, &printb, &b1, mFtraceDriver)) {
            if (mDriver.getConfig().is_system_wide) {
                logg.logError("readProcSysDependencies failed");
                handleException();
            }
            else {
                logg.logMessage("readProcSysDependencies failed");
            }
        }
        mAttrsBuffer->flush();

        // Postpone reading kallsyms as on android adb gets too backed up and data is lost
        procThreadArgs.mProcBuffer = mProcBuffer.get();
        procThreadArgs.mIsDone = false;
        if (pthread_create(&procThread, nullptr, procFunc, &procThreadArgs) != 0) {
            logg.logError("pthread_create failed");
            handleException();
        }
    }

    // monitor online cores if no uevents
    std::unique_ptr<PerfCpuOnlineMonitor> onlineMonitorThread;
    if (!mUEvent.enabled()) {
        onlineMonitorThread.reset(new PerfCpuOnlineMonitor([&](unsigned cpu, bool online) -> void {
            logg.logMessage("CPU online state changed: %u -> %s", cpu, (online ? "online" : "offline"));
            const uint64_t currTime = getTime() - monotonicStart;
            if (online) {
                handleCpuOnline(currTime, cpu);
            }
            else {
                handleCpuOffline(currTime, cpu);
            }
        }));
    }

    // start sync threads
    if (mSyncThread != nullptr) {
        mSyncThread->start(monotonicStart);
    }

    // start profiling
    mProfilingStartedCallback();

    const uint64_t NO_RATE = ~0ULL;
    const uint64_t rate = gSessionData.mLiveRate > 0 && gSessionData.mSampleRate > 0 ? gSessionData.mLiveRate : NO_RATE;
    uint64_t nextTime = 0;
    int timeout = rate != NO_RATE ? 0 : -1;
    while (true) {
        // +1 for uevents, +1 for pipe
        std::vector<struct epoll_event> events {mCpuInfo.getNumberOfCores() + 2};
        int ready = mMonitor.wait(events.data(), events.size(), timeout);
        if (ready < 0) {
            logg.logError("Monitor::wait failed");
            handleException();
        }
        const uint64_t currTime = getTime() - monotonicStart;

        for (int i = 0; i < ready; ++i) {
            if (events[i].data.fd == mUEvent.getFd()) {
                if (!handleUEvent(currTime)) {
                    logg.logError("PerfSource::handleUEvent failed");
                    handleException();
                }
                break;
            }
            else if (events[i].data.fd == *mInterruptRead) {
                goto exitOuterLoop;
            }
        }

        // send a notification that data is ready
        sem_post(&mSenderSem);

        // In one shot mode, stop collection once all the buffers are filled
        if (gSessionData.mOneShot && ((mSummary.bytesAvailable() <= 0) || (mAttrsBuffer->bytesAvailable() <= 0) ||
                                      (mProcBuffer->bytesAvailable() <= 0) || mCountersBuf.isFull())) {
            logg.logMessage("One shot (perf)");
            endSession();
        }

        if (rate != NO_RATE) {
            while (currTime > nextTime) {
                nextTime += rate;
            }
            // + NS_PER_MS - 1 to ensure always rounding up
            timeout = std::max<int>(0, (nextTime + NS_PER_MS - 1 - getTime() + monotonicStart) / NS_PER_MS);
        }
    }
exitOuterLoop:

    if (onlineMonitorThread) {
        onlineMonitorThread->terminate();
    }

    procThreadArgs.mIsDone = true;
    pthread_join(procThread, nullptr);
    mCountersGroup.stop();

    // terminate all remaining sync threads
    if (mSyncThread != nullptr) {
        mSyncThread->terminate();
    }

    mIsDone = true;

    // send a notification that data is ready
    sem_post(&mSenderSem);
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
            logg.logError("Only %zu cores are expected but core %i reports %s",
                          mCpuInfo.getNumberOfCores(),
                          cpu,
                          result.mAction);
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
    const std::pair<OnlineResult, std::string> result = mCountersGroup.onlineCPU(
        cpu,
        mAppTids,
        OnlineEnabledState::ENABLE_NOW,
        *mAttrsBuffer, //
        [this](int fd) -> bool { return mMonitor.add(fd); },
        [this](int fd, int cpu, bool hasAux) -> bool { return mCountersBuf.useFd(fd, cpu, hasAux); },
        &lnx::getChildTids);

    switch (result.first) {
        case OnlineResult::SUCCESS:
            // This a bit fragile, we are assuming the driver will only write one counter per CPU
            // which is true at the time of writing (just the cpu freq)
            mAttrsBuffer->perfCounterHeader(currTime, 1);
            mDriver.read(*mAttrsBuffer, cpu);
            mAttrsBuffer->perfCounterFooter();
            // fall through
            /* no break */
        case OnlineResult::CPU_OFFLINE:
            ret = true;
            break;
        default:
            ret = false;
            break;
    }

    mAttrsBuffer->flush();

    mCpuInfo.updateIds(true);
    mDriver.coreName(mSummary, cpu);
    mSummary.flush();
    return ret;
}

bool PerfSource::handleCpuOffline(uint64_t currTime, unsigned cpu)
{
    const bool ret = mCountersGroup.offlineCPU(cpu, [this](int cpu) { mCountersBuf.discard(cpu); });
    mAttrsBuffer->offlineCPU(currTime, cpu);
    return ret;
}

void PerfSource::interrupt()
{
    int8_t c = 0;
    // Write to the pipe to wake the monitor which will cause mSessionIsActive to be reread
    if (::write(*mInterruptWrite, &c, sizeof(c)) != sizeof(c)) {
        logg.logError("write failed");
        handleException();
    }
}

bool PerfSource::write(ISender & sender)
{
    // check mIsDone before we write so we guarantee the
    // buffers won't have anymore added after we return
    const bool done = mIsDone;

    mSummary.write(sender);
    mAttrsBuffer->write(sender);
    mProcBuffer->write(sender);
    if (!mCountersBuf.send(sender)) {
        logg.logError("PerfBuffer::send failed");
        handleException();
    }
    // This is racey, unless we assume no one posts reader sem before profiling started
    if (mSyncThread != nullptr) {
        mSyncThread->send(sender);
    }

    return done;
}
