/**
 * Copyright (C) Arm Limited 2014-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FTRACEDRIVER_H
#define FTRACEDRIVER_H

#include <pthread.h>
#include <utility>
#include <vector>

#include "ClassBoilerPlate.h"
#include "SimpleDriver.h"

class DynBuf;
class IPerfAttrsConsumer;

// The Android NDK doesn't provide an implementation of pthread_barrier_t, so implement our own
class Barrier
{
public:
    Barrier();
    ~Barrier();

    void init(unsigned int count);
    void wait();

private:
    pthread_mutex_t mMutex;
    pthread_cond_t mCond;
    unsigned int mCount;
};

class FtraceDriver : public SimpleDriver
{
public:
    FtraceDriver(bool useForTracepoint, size_t numberOfCores);
    ~FtraceDriver();

    void readEvents(mxml_node_t * const xml);

    std::pair<std::vector<int>, bool> prepare();
    void start();
    std::vector<int> stop();
    bool readTracepointFormats(const uint64_t currTime, IPerfAttrsConsumer & attrsConsumer, DynBuf * const printb, DynBuf * const b);

    bool isSupported() const
    {
        return mSupported;
    }

private:
    int64_t *mValues;
    Barrier mBarrier;
    int mTracingOn;
    bool mSupported, mMonotonicRawSupport, mUseForTracepoints;
    size_t mNumberOfCores;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(FtraceDriver);
};

#endif // FTRACEDRIVER_H
