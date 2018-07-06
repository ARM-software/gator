/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PERF_GROUP
#define PERF_GROUP

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "ClassBoilerPlate.h"
#include "Config.h"
#include "linux/perf/PerfEventGroupIdentifier.h"
#include "linux/perf/PerfEventGroup.h"

class Buffer;
class GatorCpu;
class Monitor;
class PerfBuffer;

class PerfGroups
{
public:
    PerfGroups(PerfBuffer * pb, const PerfConfig & config);

    bool createCpuGroup(const uint64_t currTime, Buffer * const buffer);
    bool add(uint64_t timestamp, Buffer * buffer, const PerfEventGroupIdentifier & groupIdentifier, int key,
             uint32_t type, uint64_t config, uint64_t periodOrFreq, uint64_t sampleType, int flags);
    PerfEventGroup& addGroupLeader(const uint64_t timestamp, Buffer * const buffer, const PerfEventGroupIdentifier & groupIdentifier);
    /**
     *
     * @param currTime
     * @param cpu
     * @param appTid the tid to count or -1 if all thread or 0 for current
     * @param enableNow
     * @param buffer
     * @param monitor
     * @return
     * @note Not safe to call concurrently.
     */
    OnlineResult onlineCPU(const uint64_t currTime, const int cpu, const std::set<int> & appPids, const bool enableNow, Buffer * const buffer,
                           Monitor * const monitor);
    bool offlineCPU(int cpu);
    void start();
    void stop();

private:

    PerfEventGroupSharedConfig sharedConfig;
    std::map<PerfEventGroupIdentifier, std::unique_ptr<PerfEventGroup>> perfEventGroupMap;
    PerfBuffer * const mPb;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(PerfGroups);
};

#endif // PERF_GROUP
