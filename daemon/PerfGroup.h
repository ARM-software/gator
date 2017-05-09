/**
 * Copyright (C) ARM Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PERF_GROUP
#define PERF_GROUP

#include <stdint.h>

// Use a snapshot of perf_event.h as it may be more recent than what is on the target and if not newer features won't be supported anyways
#include "k/perf_event.h"

#include "ClassBoilerPlate.h"
#include "Config.h"

class Buffer;
class GatorCpu;
class Monitor;
class PerfBuffer;

enum PerfGroupFlags
{
    PERF_GROUP_MMAP = 1 << 0,
    PERF_GROUP_COMM = 1 << 1,
    PERF_GROUP_FREQ = 1 << 2,
    PERF_GROUP_TASK = 1 << 3,
    PERF_GROUP_SAMPLE_ID_ALL = 1 << 4,
    PERF_GROUP_PER_CPU = 1 << 5,
    PERF_GROUP_LEADER = 1 << 6,
    PERF_GROUP_CPU = 1 << 7,
    PERF_GROUP_ALL_CLUSTERS = 1 << 8,
};

enum
{
    PG_SUCCESS = 0,
    PG_FAILURE,
    PG_CPU_OFFLINE,
};

class PerfGroup
{
public:
    PerfGroup(PerfBuffer * pb, bool legacySupport, bool clockIdSupport);
    ~PerfGroup();

    bool createCpuGroup(const uint64_t currTime, Buffer * const buffer);
    bool add(const uint64_t currTime, Buffer * const buffer, const int key, const __u32 type, const __u64 config,
             const __u64 sample, const __u64 sampleType, const int flags, const GatorCpu * const cluster);
    // Safe to call concurrently
    int prepareCPU(const int cpu, Monitor * const monitor);
    // Not safe to call concurrently. Returns the number of events enabled
    int onlineCPU(const uint64_t currTime, const int cpu, const bool enable, Buffer * const buffer);
    bool offlineCPU(int cpu);
    void start();
    void stop();

private:
    int getEffectiveType(const int type, const int flags);
    int doAdd(const uint64_t currTime, Buffer * const buffer, const int key, const __u32 type, const __u64 config,
              const __u64 sample, const __u64 sampleType, const int flags, const GatorCpu * const cluster);

    // 2* to be conservative for sched_switch, cpu_idle, hrtimer and non-CPU groups
    struct perf_event_attr mAttrs[2 * MAX_PERFORMANCE_COUNTERS];
    PerfBuffer * const mPb;
    const GatorCpu *mClusters[2 * MAX_PERFORMANCE_COUNTERS];
    int mFlags[2 * MAX_PERFORMANCE_COUNTERS];
    int mKeys[2 * MAX_PERFORMANCE_COUNTERS];
    int mFds[NR_CPUS * (2 * MAX_PERFORMANCE_COUNTERS)];
    // Offset in mAttrs, mFlags, mClusters and mKeys of the group leaders for each perf type
    int mLeaders[16];
    int mSchedSwitchId;
    bool mLegacySupport;
    bool mClockIdSupport;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(PerfGroup);
};

#endif // PERF_GROUP
