/* Copyright (C) 2014-2021 by Arm Limited. All rights reserved. */

#ifndef FTRACEDRIVER_H
#define FTRACEDRIVER_H

#include "SimpleDriver.h"
#include "Tracepoints.h"

#include <utility>
#include <vector>

#include <pthread.h>

class DynBuf;
class IPerfAttrsConsumer;

// The Android NDK doesn't provide an implementation of pthread_barrier_t, so implement our own
class Barrier {
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

class FtraceDriver : public SimpleDriver {
public:
    FtraceDriver(const TraceFsConstants & traceFsConstants, bool useForTracepoint, size_t numberOfCores);

    // Intentionally unimplemented
    FtraceDriver(const FtraceDriver &) = delete;
    FtraceDriver & operator=(const FtraceDriver &) = delete;
    FtraceDriver(FtraceDriver &&) = delete;
    FtraceDriver & operator=(FtraceDriver &&) = delete;

    void readEvents(mxml_node_t * xml) override;

    std::pair<std::vector<int>, bool> prepare();
    void start();
    std::vector<int> stop();
    bool readTracepointFormats(IPerfAttrsConsumer & attrsConsumer, DynBuf * printb, DynBuf * b);

    bool isSupported() const { return mSupported; }

private:
    const TraceFsConstants & traceFsConstants;
    Barrier mBarrier;
    int mTracingOn;
    bool mSupported, mMonotonicRawSupport, mUseForTracepoints;
    size_t mNumberOfCores;
};

#endif // FTRACEDRIVER_H
