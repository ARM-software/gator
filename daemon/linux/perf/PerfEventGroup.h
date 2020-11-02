/* Copyright (C) 2018-2020 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_PERF_PERF_EVENT_GROUP_H
#define INCLUDE_LINUX_PERF_PERF_EVENT_GROUP_H

#include "Tracepoints.h"
#include "k/perf_event.h" // Use a snapshot of perf_event.h as it may be more recent than what is on the target and if not newer features won't be supported anyways
#include "lib/AutoClosingFd.h"
#include "lib/Span.h"
#include "linux/perf/IPerfGroups.h"
#include "linux/perf/PerfConfig.h"
#include "linux/perf/PerfEventGroupIdentifier.h"

#include <climits>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <vector>

class IPerfAttrsConsumer;
class GatorCpu;

enum class OnlineResult {
    SUCCESS,
    FAILURE,
    CPU_OFFLINE,
    OTHER_FAILURE,
};

enum class OnlineEnabledState { ENABLE_NOW, ENABLE_ON_EXEC, NOT_ENABLED };

struct PerfEventGroupSharedConfig {
    inline PerfEventGroupSharedConfig(const PerfConfig & perfConfig,
                                      size_t dataBufferLength,
                                      size_t auxBufferLength,
                                      int backtraceDepth,
                                      int sampleRate,
                                      bool enablePeriodicSampling,
                                      lib::Span<const GatorCpu> clusters,
                                      lib::Span<const int> clusterIds,
                                      int64_t schedSwitchId)
        : perfConfig(perfConfig),
          schedSwitchId(schedSwitchId),
          schedSwitchKey(INT_MAX),
          dummyKeyCounter(INT_MAX - 1),
          dataBufferLength(dataBufferLength),
          auxBufferLength(auxBufferLength),
          backtraceDepth(backtraceDepth),
          sampleRate(sampleRate),
          enablePeriodicSampling(enablePeriodicSampling),
          clusters(clusters),
          clusterIds(clusterIds)
    {
    }

    const PerfConfig & perfConfig;
    /// tracepoint ID for sched_switch or UNKNOWN_TRACE_POINT_ID
    int64_t schedSwitchId;
    int schedSwitchKey;
    int dummyKeyCounter;
    size_t dataBufferLength;
    size_t auxBufferLength;
    int backtraceDepth;
    int sampleRate;
    bool enablePeriodicSampling;
    lib::Span<const GatorCpu> clusters;
    lib::Span<const int> clusterIds;
};

class PerfEventGroup {
public:
    PerfEventGroup(const PerfEventGroupIdentifier & groupIdentifier, PerfEventGroupSharedConfig & sharedConfig);

    bool requiresLeader() const;
    bool hasLeader() const;
    bool addEvent(bool leader,
                  IPerfAttrsConsumer & attrsConsumer,
                  int key,
                  const IPerfGroups::Attr & attr,
                  bool hasAuxData);
    bool createGroupLeader(IPerfAttrsConsumer & attrsConsumer);

    std::pair<OnlineResult, std::string> onlineCPU(int cpu,
                                                   std::set<int> & tids,
                                                   OnlineEnabledState enabledState,
                                                   IPerfAttrsConsumer & attrsConsumer,
                                                   const std::function<bool(int)> & addToMonitor,
                                                   const std::function<bool(int, int, bool)> & addToBuffer);

    bool offlineCPU(int cpu);
    void start();
    void stop();

private:
    struct PerfEvent {
        struct perf_event_attr attr;
        int key;
    };

    PerfEventGroup(const PerfEventGroup &) = delete;
    PerfEventGroup & operator=(const PerfEventGroup &) = delete;
    PerfEventGroup(PerfEventGroup &&) = delete;
    PerfEventGroup & operator=(PerfEventGroup &&) = delete;

    bool createCpuGroupLeader(IPerfAttrsConsumer & attrsConsumer);
    bool createUncoreGroupLeader(IPerfAttrsConsumer & attrsConsumer);

    bool enable(const std::map<int, std::map<int, lib::AutoClosingFd>> & eventIndexToTidToFdMap);
    bool checkEnabled(const std::map<int, std::map<int, lib::AutoClosingFd>> & eventIndexToTidToFdMap);

    int nextDummyKey();

    const PerfEventGroupIdentifier groupIdentifier;
    PerfEventGroupSharedConfig & sharedConfig;

    // list of events associated with the group, where the first must be the group leader
    std::vector<PerfEvent> events;

    // map from cpu -> (map from mEvents index -> (map from tid -> file descriptor))
    std::map<int, std::map<int, std::map<int, lib::AutoClosingFd>>> cpuToEventIndexToTidToFdMap;
};

#endif /* INCLUDE_LINUX_PERF_PERF_EVENT_GROUP_H */
