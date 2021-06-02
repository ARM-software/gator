/* Copyright (C) 2010-2021 by Arm Limited. All rights reserved. */

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
#include <semaphore.h>
#include <set>

class PerfDriver;
class ISender;
class FtraceDriver;
class ICpuInfo;

class PerfSource : public PrimarySource {
public:
    PerfSource(PerfDriver & driver,
               sem_t & senderSem,
               std::function<void()> profilingStartedCallback,
               std::set<int> appTids,
               FtraceDriver & ftraceDriver,
               bool enableOnCommandExec,
               ICpuInfo & cpuInfo);

    bool prepare();
    lib::Optional<uint64_t> sendSummary() override;
    void run(std::uint64_t, std::function<void()> endSession) override;
    void interrupt() override;
    bool write(ISender & sender) override;

private:
    bool handleUEvent(uint64_t currTime);
    bool handleCpuOnline(uint64_t currTime, unsigned cpu);
    bool handleCpuOffline(uint64_t currTime, unsigned cpu);

    SummaryBuffer mSummary;
    Buffer mMemoryBuffer;
    PerfToMemoryBuffer mPerfToMemoryBuffer;
    PerfBuffer mCountersBuf;
    PerfGroups mCountersGroup;
    Monitor mMonitor;
    UEvent mUEvent;
    std::set<int> mAppTids;
    PerfDriver & mDriver;
    std::unique_ptr<PerfAttrsBuffer> mAttrsBuffer;
    std::unique_ptr<PerfAttrsBuffer> mProcBuffer;
    sem_t & mSenderSem;
    std::function<void()> mProfilingStartedCallback;
    lib::AutoClosingFd mInterruptRead {};
    lib::AutoClosingFd mInterruptWrite {};
    std::atomic_bool mIsDone;
    FtraceDriver & mFtraceDriver;
    ICpuInfo & mCpuInfo;
    std::unique_ptr<PerfSyncThreadBuffer> mSyncThread;
    bool enableOnCommandExec;

    // Intentionally undefined
    PerfSource(const PerfSource &) = delete;
    PerfSource & operator=(const PerfSource &) = delete;
    PerfSource(PerfSource &&) = delete;
    PerfSource & operator=(PerfSource &&) = delete;
};

#endif // PERFSOURCE_H
