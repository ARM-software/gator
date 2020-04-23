/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef PERF_ATTRS_BUFFER_H
#define PERF_ATTRS_BUFFER_H

#include "Buffer.h"
#include "linux/perf/IPerfAttrsConsumer.h"

struct perf_event_attr;

class PerfAttrsBuffer : public IPerfAttrsConsumer {
public:
    PerfAttrsBuffer(int size, sem_t * const readerSem);
    ~PerfAttrsBuffer() = default;

    void write(ISender * sender);

    int bytesAvailable() const;
    void commit(const uint64_t time);

    // Perf Attrs messages
    void marshalPea(const uint64_t currTime, const struct perf_event_attr * const pea, int key) override;
    void marshalKeys(const uint64_t currTime,
                     const int count,
                     const uint64_t * const ids,
                     const int * const keys) override;
    void marshalKeysOld(const uint64_t currTime,
                        const int keyCount,
                        const int * const keys,
                        const int bytes,
                        const char * const buf) override;
    void marshalFormat(const uint64_t currTime, const int length, const char * const format) override;
    void marshalMaps(const uint64_t currTime, const int pid, const int tid, const char * const maps) override;
    void marshalComm(const uint64_t currTime,
                     const int pid,
                     const int tid,
                     const char * const image,
                     const char * const comm) override;
    void onlineCPU(const uint64_t currTime, const int cpu) override;
    void offlineCPU(const uint64_t currTime, const int cpu) override;
    void marshalKallsyms(const uint64_t currTime, const char * const kallsyms) override;
    void perfCounterHeader(const uint64_t time, const int numberOfCounters) override;
    void perfCounter(const int core, const int key, const int64_t value) override;
    void perfCounterFooter(const uint64_t currTime) override;
    void marshalHeaderPage(const uint64_t currTime, const char * const headerPage) override;
    void marshalHeaderEvent(const uint64_t currTime, const char * const headerEvent) override;

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
