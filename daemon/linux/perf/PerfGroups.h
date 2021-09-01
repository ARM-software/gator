/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

#ifndef PERF_GROUPS_H
#define PERF_GROUPS_H

#include "linux/perf/IPerfGroups.h"
#include "linux/perf/PerfEventGroup.h"
#include "linux/perf/PerfEventGroupIdentifier.h"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

class GatorCpu;

class PerfGroups : public IPerfGroups {
public:
    PerfGroups(const PerfConfig & perfConfig,
               size_t dataBufferLength,
               size_t auxBufferLength,
               int backtraceDepth,
               int sampleRate,
               bool enablePeriodicSampling,
               bool excludeKernelEvents,
               lib::Span<const GatorCpu> clusters,
               lib::Span<const int> clusterIds,
               int64_t schedSwitchId);

    PerfGroups(const PerfConfig & perfConfig,
               size_t dataBufferLength,
               size_t auxBufferLength,
               int backtraceDepth,
               int sampleRate,
               bool enablePeriodicSampling,
               bool excludeKernelEvents,
               lib::Span<const GatorCpu> clusters,
               lib::Span<const int> clusterIds,
               int64_t schedSwitchId,
               unsigned int maxFiles);

    // Intentionally undefined
    PerfGroups(const PerfGroups &) = delete;
    PerfGroups & operator=(const PerfGroups &) = delete;
    PerfGroups(PerfGroups &&) = delete;
    PerfGroups & operator=(PerfGroups &&) = delete;

    bool add(IPerfAttrsConsumer & attrsConsumer,
             const PerfEventGroupIdentifier & groupIdentifier,
             int key,
             const Attr & attr,
             bool hasAuxData = false) override;

    void addGroupLeader(IPerfAttrsConsumer & attrsConsumer, const PerfEventGroupIdentifier & groupIdentifier) override
    {
        getGroup(attrsConsumer, groupIdentifier);
    }

    /**
     * @param appPids ignored if system wide
     * @note Not safe to call concurrently.
     */
    std::pair<OnlineResult, std::string> onlineCPU(int cpu,
                                                   const std::set<int> & appPids,
                                                   OnlineEnabledState enabledState,
                                                   IPerfAttrsConsumer & attrsConsumer,
                                                   const std::function<bool(int)> & addToMonitor,
                                                   const std::function<bool(int, int, bool)> & addToBuffer,
                                                   const std::function<std::set<int>(int)> & childTids);

    bool offlineCPU(int cpu, const std::function<void(int)> & removeFromBuffer);
    void start();
    void stop();
    bool hasSPE() const;

private:
    PerfEventGroupSharedConfig sharedConfig;
    std::map<PerfEventGroupIdentifier, std::unique_ptr<PerfEventGroup>> perfEventGroupMap {};
    std::map<int, unsigned int> eventsOpenedPerCpu {};
    unsigned int maxFiles;
    unsigned int numberOfEventsAdded {0};

    /// Get the group and create the group leader if needed
    PerfEventGroup & getGroup(IPerfAttrsConsumer & attrsConsumer, const PerfEventGroupIdentifier & groupIdentifier);
};

#endif // PERF_GROUPS_H
