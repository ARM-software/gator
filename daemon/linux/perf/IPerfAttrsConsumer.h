/* Copyright (C) 2013-2024 by Arm Limited. All rights reserved. */

#ifndef IPERF_ATTRS_CONSUMER_H
#define IPERF_ATTRS_CONSUMER_H

#include "lib/Span.h"

#include <cstdint>
#include <string_view>

struct perf_event_attr;

class IPerfAttrsConsumer {
public:
    virtual ~IPerfAttrsConsumer() = default;

    enum class MetricEventType {
        // these numeric values are used by the apc from so don't change the ordnanal...
        event = 0,
        cycle_counter = 1,
        return_counter = 2,
    };

    virtual void marshalPea(const struct perf_event_attr * pea, int key) = 0;
    virtual void marshalKeys(int count, const uint64_t * ids, const int * keys) = 0;
    virtual void marshalKeysOld(int keyCount, const int * keys, int bytes, const char * buf) = 0;
    virtual void marshalFormat(int length, const char * format) = 0;
    virtual void marshalMaps(int pid, int tid, const char * maps) = 0;
    virtual void marshalComm(int pid, int tid, const char * image, const char * comm) = 0;
    virtual void onlineCPU(uint64_t time, int cpu) = 0;
    virtual void offlineCPU(uint64_t time, int cpu) = 0;
    virtual void marshalKallsyms(const char * kallsyms) = 0;
    virtual void perfCounterHeader(uint64_t time, int numberOfCounters) = 0;
    virtual void perfCounter(int core, int key, int64_t value) = 0;
    virtual void perfCounterFooter() = 0;
    virtual void marshalHeaderPage(const char * headerPage) = 0;
    virtual void marshalHeaderEvent(const char * headerEvent) = 0;
    virtual void marshalMetricKey(int metric_key, std::uint16_t event_code, int event_key, MetricEventType type) = 0;

    virtual void marshalKernelBuildId(lib::Span<std::uint8_t const> build_id) = 0;
    virtual void marshalKernelModuleBuildId(std::string_view module_name, lib::Span<std::uint8_t const> build_id) = 0;
};

#endif // PERF_ATTRS_CONSUMER_H
