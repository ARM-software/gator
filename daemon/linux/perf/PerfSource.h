/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PERFSOURCE_H
#define PERFSOURCE_H

#include <semaphore.h>
#include <set>

#include "ClassBoilerPlate.h"
#include "SummaryBuffer.h"
#include "Monitor.h"
#include "linux/perf/PerfBuffer.h"
#include "linux/perf/PerfGroups.h"
#include "Source.h"
#include "UEvent.h"

class PerfAttrsBuffer;
class PerfDriver;
class ISender;
class FtraceDriver;
class ICpuInfo;
class PerfSyncThreadBuffer;

class PerfSource : public Source
{
public:
    PerfSource(PerfDriver & driver, Child & child, sem_t & senderSem, sem_t & startProfile,
               const std::set<int> & appTids, FtraceDriver & ftraceDriver, bool enableOnCommandExec,
               ICpuInfo & cpuInfo);
    ~PerfSource();

    virtual bool prepare() override;
    virtual void run() override;
    virtual void interrupt() override;
    virtual bool isDone() override;
    virtual void write(ISender * sender) override;

private:
    bool handleUEvent(const uint64_t currTime);
    bool handleCpuOnline(uint64_t currTime, unsigned cpu);
    bool handleCpuOffline(uint64_t currTime, unsigned cpu);

    SummaryBuffer mSummary;
    PerfBuffer mCountersBuf;
    PerfGroups mCountersGroup;
    Monitor mMonitor;
    UEvent mUEvent;
    std::set<int> mAppTids;
    PerfDriver & mDriver;
    PerfAttrsBuffer *mAttrsBuffer;
    sem_t & mSenderSem;
    sem_t & mStartProfile;
    int mInterruptFd;
    bool mIsDone;
    FtraceDriver & mFtraceDriver;
    ICpuInfo & mCpuInfo;
    std::vector<std::unique_ptr<PerfSyncThreadBuffer>> mSyncThreads;
    bool enableOnCommandExec;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(PerfSource);
};

#endif // PERFSOURCE_H
