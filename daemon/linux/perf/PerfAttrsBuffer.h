/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

#ifndef PERF_ATTRS_BUFFER_H
#define PERF_ATTRS_BUFFER_H

#include "Buffer.h"
#include "linux/perf/IPerfAttrsConsumer.h"

struct perf_event_attr;

class PerfAttrsBuffer : public IPerfAttrsConsumer {
public:
    PerfAttrsBuffer(int size, sem_t & readerSem);
    ~PerfAttrsBuffer() override = default;

    // Intentionally unimplemented
    PerfAttrsBuffer(const PerfAttrsBuffer &) = delete;
    PerfAttrsBuffer & operator=(const PerfAttrsBuffer &) = delete;
    PerfAttrsBuffer(PerfAttrsBuffer &&) = delete;
    PerfAttrsBuffer & operator=(PerfAttrsBuffer &&) = delete;

    void write(ISender & sender);

    int bytesAvailable() const;
    void flush();

    // Perf Attrs messages
    void marshalPea(const struct perf_event_attr * pea, int key) override;
    void marshalKeys(int count, const uint64_t * ids, const int * keys) override;
    void marshalKeysOld(int keyCount, const int * keys, int bytes, const char * buf) override;
    void marshalFormat(int length, const char * format) override;
    void marshalMaps(int pid, int tid, const char * maps) override;
    void marshalComm(int pid, int tid, const char * image, const char * comm) override;
    void onlineCPU(uint64_t time, int cpu) override;
    void offlineCPU(uint64_t time, int cpu) override;
    void marshalKallsyms(const char * kallsyms) override;
    void perfCounterHeader(uint64_t time, int numberOfCounters) override;
    void perfCounter(int core, int key, int64_t value) override;
    void perfCounterFooter() override;
    void marshalHeaderPage(const char * headerPage) override;
    void marshalHeaderEvent(const char * headerEvent) override;

private:
    void waitForSpace(int bytes);

    Buffer buffer;
};

#endif // PERF_ATTRS_BUFFER_H
