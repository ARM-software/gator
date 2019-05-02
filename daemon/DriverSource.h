/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef DRIVERSOURCE_H
#define DRIVERSOURCE_H

#include <semaphore.h>
#include <stdint.h>

#include "Source.h"

class PerfAttrsBuffer;
class Fifo;
class FtraceDriver;

class DriverSource : public Source
{
public:
    DriverSource(Child & child, sem_t & senderSem, sem_t & startProfile, FtraceDriver & ftraceDriver);
    ~DriverSource();

    virtual bool prepare() override;
    virtual void run() override;
    virtual void interrupt() override;
    virtual bool isDone() override;
    virtual void write(ISender * sender) override;

private:
    static void *bootstrapThreadStatic(void *arg);
    void bootstrapThread();

    PerfAttrsBuffer *mBuffer;
    Fifo *mFifo;
    sem_t & mSenderSem;
    sem_t & mStartProfile;
    int mBufferSize;
    int mBufferFD;
    int mLength;
    FtraceDriver & mFtraceDriver;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(DriverSource);
};

#endif // DRIVERSOURCE_H
