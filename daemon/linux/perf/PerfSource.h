/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#ifndef PERFSOURCE_H
#define PERFSOURCE_H

#include "Monitor.h"
#include "Source.h"
#include "SummaryBuffer.h"
#include "UEvent.h"
#include "linux/perf/PerfBuffer.h"
#include "linux/perf/PerfGroups.h"

#include <functional>
#include <semaphore.h>
#include <set>

class PerfAttrsBuffer;
class PerfDriver;
class ISender;
class FtraceDriver;
class ICpuInfo;
class PerfSyncThreadBuffer;

class PerfSource : public Source {
public:
    PerfSource(PerfDriver & driver,
               Child & child,
               sem_t & senderSem,
               std::function<void()> profilingStartedCallback,
               std::set<int> appTids,
               FtraceDriver & ftraceDriver,
               bool enableOnCommandExec,
               ICpuInfo & cpuInfo);

    virtual bool prepare() override;
    virtual void run() override;
    virtual void interrupt() override;
    virtual bool isDone() override;
    virtual void write(ISender & sender) override;

private:
    bool handleUEvent(uint64_t currTime);
    bool handleCpuOnline(uint64_t currTime, unsigned cpu);
    bool handleCpuOffline(uint64_t currTime, unsigned cpu);

    SummaryBuffer mSummary;
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
    int mInterruptFd;
    bool mIsDone;
    FtraceDriver & mFtraceDriver;
    ICpuInfo & mCpuInfo;
    std::vector<std::unique_ptr<PerfSyncThreadBuffer>> mSyncThreads;
    bool enableOnCommandExec;

    // Intentionally undefined
    PerfSource(const PerfSource &) = delete;
    PerfSource & operator=(const PerfSource &) = delete;
    PerfSource(PerfSource &&) = delete;
    PerfSource & operator=(PerfSource &&) = delete;
};

#endif // PERFSOURCE_H
