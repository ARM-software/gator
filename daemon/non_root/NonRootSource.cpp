/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#include "non_root/NonRootSource.h"
#include "non_root/NonRootDriver.h"
#include "non_root/GlobalPoller.h"
#include "non_root/GlobalStatsTracker.h"
#include "non_root/GlobalStateChangeHandler.h"
#include "non_root/ProcessPoller.h"
#include "non_root/ProcessStateChangeHandler.h"
#include "lib/Time.h"
#include "Child.h"
#include "ICpuInfo.h"
#include "Logging.h"
#include "Protocol.h"
#include "SessionData.h"

#include <sys/prctl.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace non_root
{
    NonRootSource::NonRootSource(NonRootDriver & driver_, Child & child_, sem_t & senderSem_, sem_t & startProfile_, const ICpuInfo & cpuInfo)
            : Source(child_),
              mSwitchBuffers(FrameType::SCHED_TRACE, 1 * 1024 * 1024, senderSem_),
              mGlobalCounterBuffer(0, FrameType::BLOCK_COUNTER, 1 * 1024 * 1024, &senderSem_),
              mProcessCounterBuffer(0, FrameType::BLOCK_COUNTER, 1 * 1024 * 1024, &senderSem_),
              mMiscBuffer(0, FrameType::UNKNOWN, 1 * 1024 * 1024, &senderSem_),
              interrupted(false),
              timestampSource(CLOCK_MONOTONIC_RAW),
              driver(driver_),
              senderSem(senderSem_),
              startProfile(startProfile_),
              done(false),
              cpuInfo(cpuInfo)

    {
    }

    bool NonRootSource::prepare()
    {
        return summary();
    }

    void NonRootSource::run()
    {
        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-nrsrc"), 0, 0, 0);

        std::map<NonRootCounter, int> enabledCounters = driver.getEnabledCounters();

        // clktck and page size values
        const long clktck = sysconf(_SC_CLK_TCK);
        const long pageSize = sysconf(_SC_PAGESIZE);

        // global stuff
        GlobalStateChangeHandler globalChangeHandler(mGlobalCounterBuffer, enabledCounters);
        GlobalStatsTracker globalStatsTracker(globalChangeHandler);
        GlobalPoller globalPoller(globalStatsTracker, timestampSource);

        // process related stuff
        ProcessStateChangeHandler processChangeHandler(mProcessCounterBuffer, mMiscBuffer, mSwitchBuffers, enabledCounters);
        ProcessStateTracker processStateTracker(processChangeHandler, getBootTimeTicksBase(), clktck, pageSize);
        ProcessPoller processPoller(processStateTracker, timestampSource);

        sem_post(&startProfile);

        const useconds_t sleepIntervalUs = (gSessionData.mSampleRate < 1000 ? 10000 : 1000); // select 1ms or 10ms depending on normal or low rate

        while (gSessionData.mSessionIsActive) {
            // check buffer not full
            if (gSessionData.mOneShot && gSessionData.mSessionIsActive && ((mGlobalCounterBuffer.bytesAvailable() <= 0) ||
                    (mProcessCounterBuffer.bytesAvailable() <= 0) ||
                    (mMiscBuffer.bytesAvailable() <= 0) ||
                    mSwitchBuffers.anyFull())) {
                logg.logMessage("One shot (nrsrc)");
                mChild.endSession();
            }

            // update global stats
            globalPoller.poll();

            // update process stats
            processPoller.poll();

            // sleep an amount of time to align to the next 1 or 10 millisecond boundary depending on rate
            const unsigned long long timestampNowUs = (timestampSource.getTimestampNS() + 500) / 1000; // round to nearest uS
            const useconds_t sleepUs = sleepIntervalUs - (timestampNowUs % sleepIntervalUs);

            usleep(sleepUs);
        }

        mGlobalCounterBuffer.setDone();
        mProcessCounterBuffer.setDone();
        mMiscBuffer.setDone();
        mSwitchBuffers.setDone();
        done = true;
    }

    void NonRootSource::interrupt()
    {
        interrupted.store(true, std::memory_order_seq_cst);
    }

    bool NonRootSource::isDone()
    {
        return done && mGlobalCounterBuffer.isDone() && mProcessCounterBuffer.isDone() && mMiscBuffer.isDone() && mSwitchBuffers.allDone();
    }

    void NonRootSource::write(ISender * sender)
    {
        if (!mGlobalCounterBuffer.isDone()) {
            mGlobalCounterBuffer.write(sender);
        }
        if (!mProcessCounterBuffer.isDone()) {
            mProcessCounterBuffer.write(sender);
        }
        if (!mMiscBuffer.isDone()) {
            mMiscBuffer.write(sender);
        }
        mSwitchBuffers.write(sender);
    }

    bool NonRootSource::summary()
    {
        struct utsname utsname;
        if (uname(&utsname) != 0) {
            logg.logMessage("uname failed");
            return false;
        }

        char buf[512];
        snprintf(buf, sizeof(buf), "%s %s %s %s %s GNU/Linux", utsname.sysname, utsname.nodename, utsname.release,
                 utsname.version, utsname.machine);

        long pageSize = sysconf(_SC_PAGESIZE);
        if (pageSize < 0) {
            logg.logMessage("sysconf _SC_PAGESIZE failed");
            return false;
        }

        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
            logg.logMessage("clock_gettime failed");
            return false;
        }
        const int64_t timestamp = ts.tv_sec * NS_PER_S + ts.tv_nsec;

        const uint64_t monotonicStarted = timestampSource.getBaseTimestampNS();
        gSessionData.mMonotonicStarted = monotonicStarted;
        const uint64_t currTime = 0;

        MixedFrameBuffer miscBuffer (mMiscBuffer);

        // send summary message
        miscBuffer.summaryFrameSummaryMessage(currTime, timestamp, monotonicStarted, monotonicStarted, buf, pageSize, true);
        gSessionData.mSentSummary = true;

        for (size_t cpu = 0; cpu < cpuInfo.getNumberOfCores(); ++cpu) {
            const int cpuId = cpuInfo.getCpuIds()[cpu];
            // Don't send information on a cpu we know nothing about
            if (cpuId == -1) {
                continue;
            }

            const GatorCpu * const gatorCpu = driver.getPmuXml().findCpuById(cpuId);
            if (gatorCpu != nullptr) {
                miscBuffer.summaryFrameCoreNameMessage(currTime, cpu, cpuId, gatorCpu->getCoreName());
            }
            else {
                snprintf(buf, sizeof(buf), "Unknown (0x%.3x)", cpuId);
                miscBuffer.summaryFrameCoreNameMessage(currTime, cpu, cpuId, buf);
            }
        }

        return true;
    }

    unsigned long long NonRootSource::getBootTimeTicksBase()
    {
        lib::TimestampSource bootTime(CLOCK_BOOTTIME);

        const auto monotonicRelNs = timestampSource.getTimestampNS();
        const auto bootTimeNowNS = bootTime.getAbsTimestampNS();

        return bootTimeNowNS - monotonicRelNs;
    }
}
