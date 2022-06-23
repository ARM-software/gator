/* Copyright (C) 2010-2022 by Arm Limited. All rights reserved. */

#ifndef PERFSOURCE_H
#define PERFSOURCE_H

#include "Buffer.h"
#include "Monitor.h"
#include "Source.h"
#include "SummaryBuffer.h"
#include "UEvent.h"
#include "lib/AutoClosingFd.h"
#include "linux/perf/PerfAttrsBuffer.h"
#include "linux/perf/PerfBuffer.h"
#include "linux/perf/PerfGroups.h"
#include "linux/perf/PerfSyncThreadBuffer.h"
#include "linux/perf/PerfToMemoryBuffer.h"

#include <atomic>
#include <functional>
#include <memory>
#include <set>

#include <semaphore.h>

class ISender;
class FtraceDriver;
class ICpuInfo;

static constexpr auto MEGABYTES = 1024 * 1024;

class PerfSource : public PrimarySource {
public:
    static perf_ringbuffer_config_t createPerfBufferConfig();

    PerfSource(perf_event_group_activator_config_t const & configuration,
               perf_groups_activator_state_t && state,
               std::unique_ptr<PerfAttrsBuffer> && attrs_buffer,
               sem_t & senderSem,
               std::function<void()> profilingStartedCallback,
               std::function<std::optional<uint64_t>(ISummaryConsumer &, std::function<uint64_t()>)> sendSummaryFn,
               std::function<void(ISummaryConsumer &, int)> coreNameFn,
               std::function<void(IPerfAttrsConsumer &, int)> readCountersFn,
               std::set<int> appTids,
               FtraceDriver & ftraceDriver,
               bool enableOnCommandExec,
               ICpuInfo & cpuInfo);

    // Intentionally undefined
    PerfSource(const PerfSource &) = delete;
    PerfSource & operator=(const PerfSource &) = delete;
    PerfSource(PerfSource &&) = delete;
    PerfSource & operator=(PerfSource &&) = delete;

    bool prepare();
    std::optional<uint64_t> sendSummary() override;
    void run(std::uint64_t, std::function<void()> endSession) override;
    void interrupt() override;
    bool write(ISender & sender) override;

private:
    PerfConfig const & mConfig;
    SummaryBuffer mSummary;
    Buffer mMemoryBuffer;
    PerfToMemoryBuffer mPerfToMemoryBuffer;
    PerfBuffer mCountersBuf;
    perf_groups_activator_state_t mCountersGroupState;
    perf_groups_activator_t mCountersGroup;
    Monitor mMonitor {};
    UEvent mUEvent {};
    std::set<int> mAppTids;
    sem_t & mSenderSem;
    std::unique_ptr<PerfAttrsBuffer> mAttrsBuffer;
    PerfAttrsBuffer mProcBuffer;
    std::function<void()> mProfilingStartedCallback;
    std::function<std::optional<uint64_t>(ISummaryConsumer &, std::function<uint64_t()>)> mSendSummaryFn;
    std::function<void(ISummaryConsumer &, int)> mCoreNameFn;
    std::function<void(IPerfAttrsConsumer &, int)> mReadCountersFn;
    lib::AutoClosingFd mInterruptRead {};
    lib::AutoClosingFd mInterruptWrite {};
    std::atomic_bool mIsDone {false};
    FtraceDriver & mFtraceDriver;
    ICpuInfo & mCpuInfo;
    std::unique_ptr<PerfSyncThreadBuffer> mSyncThread {};
    bool enableOnCommandExec {false};

    bool handleUEvent(uint64_t currTime);
    bool handleCpuOnline(uint64_t currTime, unsigned cpu);
    bool handleCpuOffline(uint64_t currTime, unsigned cpu);
};

#endif // PERFSOURCE_H
