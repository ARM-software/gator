/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef PERF_ATTRS_BUFFER_H
#define PERF_ATTRS_BUFFER_H

#include "Buffer.h"
#include "linux/perf/IPerfAttrsConsumer.h"

struct perf_event_attr;

class PerfAttrsBuffer : public IPerfAttrsConsumer {
public:
    PerfAttrsBuffer(int size, sem_t & readerSem);
    ~PerfAttrsBuffer() override = default;

    void write(ISender & sender);

    int bytesAvailable() const;
    void commit(uint64_t time);

    // Perf Attrs messages
    void marshalPea(uint64_t currTime, const struct perf_event_attr * pea, int key) override;
    void marshalKeys(uint64_t currTime, int count, const uint64_t * ids, const int * keys) override;
    void marshalKeysOld(uint64_t currTime, int keyCount, const int * keys, int bytes, const char * buf) override;
    void marshalFormat(uint64_t currTime, int length, const char * format) override;
    void marshalMaps(uint64_t currTime, int pid, int tid, const char * maps) override;
    void marshalComm(uint64_t currTime, int pid, int tid, const char * image, const char * comm) override;
    void onlineCPU(uint64_t currTime, int cpu) override;
    void offlineCPU(uint64_t currTime, int cpu) override;
    void marshalKallsyms(uint64_t currTime, const char * kallsyms) override;
    void perfCounterHeader(uint64_t time, int numberOfCounters) override;
    void perfCounter(int core, int key, int64_t value) override;
    void perfCounterFooter(uint64_t currTime) override;
    void marshalHeaderPage(uint64_t currTime, const char * headerPage) override;
    void marshalHeaderEvent(uint64_t currTime, const char * headerEvent) override;

    void setDone();
    bool isDone() const;

private:
    Buffer buffer;
    // Intentionally unimplemented
    PerfAttrsBuffer(const PerfAttrsBuffer &) = delete;
    PerfAttrsBuffer & operator=(const PerfAttrsBuffer &) = delete;
    PerfAttrsBuffer(PerfAttrsBuffer &&) = delete;
    PerfAttrsBuffer & operator=(PerfAttrsBuffer &&) = delete;
};

#endif // PERF_ATTRS_BUFFER_H
