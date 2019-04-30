/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PERF_GROUPS_H
#define PERF_GROUPS_H

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "ClassBoilerPlate.h"
#include "linux/perf/PerfEventGroupIdentifier.h"
#include "linux/perf/PerfEventGroup.h"
#include "linux/perf/IPerfGroups.h"

class GatorCpu;

class PerfGroups : public IPerfGroups
{
public:
    PerfGroups(const PerfConfig & perfConfig, size_t bufferLength, int backtraceDepth,
               int sampleRate, bool isEbs, lib::Span<const GatorCpu> clusters, lib::Span<const int> clusterIds, int64_t schedSwitchId);

    virtual bool add(uint64_t timestamp, IPerfAttrsConsumer & attrsConsumer,
                     const PerfEventGroupIdentifier & groupIdentifier, int key, const Attr & attr,
                     bool hasAuxData = false) override;

    virtual void addGroupLeader(const uint64_t timestamp, IPerfAttrsConsumer & attrsConsumer,
                        const PerfEventGroupIdentifier & groupIdentifier) override {
        getGroup(timestamp, attrsConsumer, groupIdentifier);
    }

    /**
     *
     * @param currTime
     * @param cpu
     * @param appPids ignored if system wide
     * @param enableNow
     * @return
     * @note Not safe to call concurrently.
     */
    OnlineResult onlineCPU(uint64_t timestamp, int cpu, const std::set<int> & appPids, OnlineEnabledState enabledState,
                           IPerfAttrsConsumer & attrsConsumer, std::function<bool(int)> addToMonitor,
                           std::function<bool(int, int, bool)> addToBuffer,
                           std::function<std::set<int>(int)> childTids);

    bool offlineCPU(int cpu, std::function<void(int)> removeFromBuffer);
    void start();
    void stop();
    bool hasSPE() const;

private:
    /// Get the group and create the group leader if needed
    PerfEventGroup& getGroup(const uint64_t timestamp, IPerfAttrsConsumer & attrsConsumer, const PerfEventGroupIdentifier & groupIdentifier);

    PerfEventGroupSharedConfig sharedConfig;
    std::map<PerfEventGroupIdentifier, std::unique_ptr<PerfEventGroup>> perfEventGroupMap;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(PerfGroups);
};

#endif // PERF_GROUPS_H
