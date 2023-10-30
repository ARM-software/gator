/* Copyright (C) 2017-2023 by Arm Limited. All rights reserved. */
#define BUFFER_USE_SESSION_DATA

#include "non_root/NonRootSource.h"

#include "BlockCounterFrameBuilder.h"
#include "BlockCounterMessageConsumer.h"
#include "ICpuInfo.h"
#include "Logging.h"
#include "SessionData.h"
#include "lib/String.h"
#include "non_root/GlobalPoller.h"
#include "non_root/GlobalStateChangeHandler.h"
#include "non_root/GlobalStatsTracker.h"
#include "non_root/NonRootDriver.h"
#include "non_root/ProcessPoller.h"
#include "non_root/ProcessStateChangeHandler.h"

#include <utility>

#include <sys/prctl.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace non_root {
    static constexpr std::size_t default_buffer_size = 1UL * 1024UL * 1024UL;

    NonRootSource::NonRootSource(NonRootDriver & driver_,
                                 sem_t & senderSem_,
                                 std::function<void()> execTargetAppCallback_,
                                 std::function<void()> profilingStartedCallback_,
                                 const ICpuInfo & cpuInfo)
        : mSwitchBuffers(default_buffer_size, senderSem_),
          mGlobalCounterBuffer(default_buffer_size, senderSem_),
          mProcessCounterBuffer(default_buffer_size, senderSem_),
          mMiscBuffer(default_buffer_size, senderSem_),
          interrupted(false),
          timestampSource(CLOCK_MONOTONIC_RAW),
          driver(driver_),
          execTargetAppCallback(std::move(execTargetAppCallback_)),
          profilingStartedCallback(std::move(profilingStartedCallback_)),
          cpuInfo(cpuInfo)

    {
    }

    void NonRootSource::run(std::uint64_t /* monotonicStarted */, std::function<void()> endSession)
    {
        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-nrsrc"), 0, 0, 0);

        std::map<NonRootCounter, int> enabledCounters = driver.getEnabledCounters();

        // clktck and page size values
        const long clktck = sysconf(_SC_CLK_TCK);
        const long pageSize = sysconf(_SC_PAGESIZE);

        // global stuff
        BlockCounterFrameBuilder globalCounterBuilder {mGlobalCounterBuffer, gSessionData.mLiveRate};
        BlockCounterMessageConsumer globalCounterConsumer {globalCounterBuilder};
        GlobalStateChangeHandler globalChangeHandler(globalCounterConsumer, enabledCounters);
        GlobalStatsTracker globalStatsTracker(globalChangeHandler);
        GlobalPoller globalPoller(globalStatsTracker, timestampSource);

        // process related stuff
        BlockCounterFrameBuilder processCounterBuilder {mProcessCounterBuffer, gSessionData.mLiveRate};
        BlockCounterMessageConsumer processCounterConsumer {processCounterBuilder};
        ProcessStateChangeHandler processChangeHandler(processCounterConsumer,
                                                       mMiscBuffer,
                                                       mSwitchBuffers,
                                                       enabledCounters);
        ProcessStateTracker processStateTracker(processChangeHandler, getBootTimeTicksBase(), clktck, pageSize);
        ProcessPoller processPoller(processStateTracker, timestampSource);

        profilingStartedCallback();
        execTargetAppCallback();

        const useconds_t sleepIntervalUs =
            (gSessionData.mSampleRate < 1000 ? 10000 : 1000); // select 1ms or 10ms depending on normal or low rate

        while (!interrupted) {
            // check buffer not full
            if (gSessionData.mOneShot
                && (mGlobalCounterBuffer.isFull() || mProcessCounterBuffer.isFull() || mMiscBuffer.isFull()
                    || mSwitchBuffers.anyFull())) {
                LOG_DEBUG("One shot (nrsrc)");
                endSession();
            }

            // update global stats
            globalPoller.poll();

            // update process stats
            processPoller.poll();

            // sleep an amount of time to align to the next 1 or 10 millisecond boundary depending on rate
            const unsigned long long timestampNowUs =
                (timestampSource.getTimestampNS() + 500) / 1000; // round to nearest uS
            const useconds_t sleepUs = sleepIntervalUs - (timestampNowUs % sleepIntervalUs);

            usleep(sleepUs);
        }

        processCounterBuilder.flush();
        globalCounterBuilder.flush();

        mGlobalCounterBuffer.setDone();
        mProcessCounterBuffer.setDone();
        mMiscBuffer.setDone();
        mSwitchBuffers.setDone();
    }

    void NonRootSource::interrupt()
    {
        interrupted.store(true, std::memory_order_seq_cst);
    }

    bool NonRootSource::write(ISender & sender)
    {
        auto const gcbw = mGlobalCounterBuffer.write(sender);
        auto const pcbw = mProcessCounterBuffer.write(sender);
        auto const mbw = mMiscBuffer.write(sender);
        auto const sbw = mSwitchBuffers.write(sender);

        return gcbw && pcbw && mbw && sbw;
    }

    std::optional<std::uint64_t> NonRootSource::sendSummary()
    {
        struct utsname utsname;
        if (uname(&utsname) != 0) {
            LOG_WARNING("uname failed");
            return {};
        }

        lib::printf_str_t<512> buf {"%s %s %s %s %s GNU/Linux",
                                    utsname.sysname,
                                    utsname.nodename,
                                    utsname.release,
                                    utsname.version,
                                    utsname.machine};

        long pageSize = sysconf(_SC_PAGESIZE);
        if (pageSize < 0) {
            LOG_WARNING("sysconf _SC_PAGESIZE failed");
            return {};
        }

        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
            LOG_WARNING("clock_gettime failed");
            return {};
        }
        const uint64_t timestamp = ts.tv_sec * NS_PER_S + ts.tv_nsec;
        const uint64_t monotonicStarted = timestampSource.getBaseTimestampNS();
        const uint64_t currTime = 0;

        MixedFrameBuffer miscBuffer(mMiscBuffer, {gSessionData.mLiveRate});

        // send summary message
        miscBuffer
            .summaryFrameSummaryMessage(currTime, timestamp, monotonicStarted, monotonicStarted, buf, pageSize, true);

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
                buf.printf("Unknown (0x%.3x)", cpuId);
                miscBuffer.summaryFrameCoreNameMessage(currTime, cpu, cpuId, buf);
            }
        }

        return {monotonicStarted};
    }

    unsigned long long NonRootSource::getBootTimeTicksBase()
    {
        lib::TimestampSource bootTime(CLOCK_BOOTTIME);

        const auto monotonicRelNs = timestampSource.getTimestampNS();
        const auto bootTimeNowNS = bootTime.getAbsTimestampNS();

        return bootTimeNowNS - monotonicRelNs;
    }
}
