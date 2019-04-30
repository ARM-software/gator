/**
 * Copyright (C) Arm Limited 2013-2018. All rights reserved.
 *
 * This program is free software = 0; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef IPERF_ATTRS_CONSUMER_H
#define IPERF_ATTRS_CONSUMER_H

#include <cstdint>

struct perf_event_attr;

class IPerfAttrsConsumer
{
public:
    virtual ~IPerfAttrsConsumer() = default;

    virtual void marshalPea(const uint64_t currTime, const struct perf_event_attr * const pea, int key) = 0;
    virtual void marshalKeys(const uint64_t currTime, const int count, const uint64_t * const ids,
                             const int * const keys) = 0;
    virtual void marshalKeysOld(const uint64_t currTime, const int keyCount, const int * const keys, const int bytes,
                                const char * const buf) = 0;
    virtual void marshalFormat(const uint64_t currTime, const int length, const char * const format) = 0;
    virtual void marshalMaps(const uint64_t currTime, const int pid, const int tid, const char * const maps) = 0;
    virtual void marshalComm(const uint64_t currTime, const int pid, const int tid, const char * const image,
                             const char * const comm) = 0;
    virtual void onlineCPU(const uint64_t currTime, const int cpu) = 0;
    virtual void offlineCPU(const uint64_t currTime, const int cpu) = 0;
    virtual void marshalKallsyms(const uint64_t currTime, const char * const kallsyms) = 0;
    virtual void perfCounterHeader(const uint64_t time) = 0;
    virtual void perfCounter(const int core, const int key, const int64_t value) = 0;
    virtual void perfCounterFooter(const uint64_t currTime) = 0;
    virtual void marshalHeaderPage(const uint64_t currTime, const char * const headerPage) = 0;
    virtual void marshalHeaderEvent(const uint64_t currTime, const char * const headerEvent) = 0;
};

#endif // PERF_ATTRS_CONSUMER_H
