/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef IPERF_ATTRS_CONSUMER_H
#define IPERF_ATTRS_CONSUMER_H

#include <cstdint>

struct perf_event_attr;

class IPerfAttrsConsumer {
public:
    virtual ~IPerfAttrsConsumer() = default;

    virtual void marshalPea(uint64_t currTime, const struct perf_event_attr * pea, int key) = 0;
    virtual void marshalKeys(uint64_t currTime, int count, const uint64_t * ids, const int * keys) = 0;
    virtual void marshalKeysOld(uint64_t currTime, int keyCount, const int * keys, int bytes, const char * buf) = 0;
    virtual void marshalFormat(uint64_t currTime, int length, const char * format) = 0;
    virtual void marshalMaps(uint64_t currTime, int pid, int tid, const char * maps) = 0;
    virtual void marshalComm(uint64_t currTime, int pid, int tid, const char * image, const char * comm) = 0;
    virtual void onlineCPU(uint64_t currTime, int cpu) = 0;
    virtual void offlineCPU(uint64_t currTime, int cpu) = 0;
    virtual void marshalKallsyms(uint64_t currTime, const char * kallsyms) = 0;
    virtual void perfCounterHeader(uint64_t time, int numberOfCounters) = 0;
    virtual void perfCounter(int core, int key, int64_t value) = 0;
    virtual void perfCounterFooter(uint64_t currTime) = 0;
    virtual void marshalHeaderPage(uint64_t currTime, const char * headerPage) = 0;
    virtual void marshalHeaderEvent(uint64_t currTime, const char * headerEvent) = 0;
};

#endif // PERF_ATTRS_CONSUMER_H
