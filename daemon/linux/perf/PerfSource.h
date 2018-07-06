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
#include "Buffer.h"
#include "Monitor.h"
#include "linux/perf/PerfBuffer.h"
#include "linux/perf/PerfGroups.h"
#include "Source.h"
#include "UEvent.h"

class PerfDriver;
class Sender;

class PerfSource : public Source
{
public:
    PerfSource(PerfDriver & driver, Child & child, sem_t & senderSem, sem_t & startProfile, const std::set<int> & appTids);
    ~PerfSource();

    virtual bool prepare() override;
    virtual void run() override;
    virtual void interrupt() override;
    virtual bool isDone() override;
    virtual void write(Sender * sender) override;

private:
    bool handleUEvent(const uint64_t currTime);

    PerfDriver & mDriver;
    Buffer mSummary;
    Buffer *mBuffer;
    PerfBuffer mCountersBuf;
    PerfGroups mCountersGroup;
    Monitor mMonitor;
    UEvent mUEvent;
    sem_t & mSenderSem;
    sem_t & mStartProfile;
    int mInterruptFd;
    bool mIsDone;
    const std::set<int> mAppTids;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(PerfSource);
};

#endif // PERFSOURCE_H
