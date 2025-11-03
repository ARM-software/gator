/* Copyright (C) 2018-2025 by Arm Limited (or its affiliates). All rights reserved. */

#ifndef INCLUDE_LINUX_PERF_PERF_EVENT_GROUP_H
#define INCLUDE_LINUX_PERF_PERF_EVENT_GROUP_H

#include "Configuration.h"
#include "agents/perf/record_types.h"
#include "k/perf_event.h" // Use a snapshot of perf_event.h as it may be more recent than what is on the target and if not newer features won't be supported anyways
#include "lib/Span.h"
#include "linux/perf/IPerfGroups.h"
#include "linux/perf/PerfConfig.h"
#include "linux/perf/PerfEventGroupIdentifier.h"
#include "linux/perf/attr_to_key_mapping_tracker.h"

#include <cstdint>
#include <limits>
#include <vector>

class GatorCpu;

/** Configuration common to both the activator and configurer */
struct perf_event_group_configurer_config_t {
    const PerfConfig & perfConfig;
    lib::Span<const GatorCpu> clusters;
    lib::Span<const int> clusterIds;
    agents::perf::buffer_config_t ringbuffer_config;
    /// tracepoint ID for sched_switch or UNKNOWN_TRACE_POINT_ID
    int64_t schedSwitchId;
    int schedSwitchKey = std::numeric_limits<int>::max();
    int dummyKeyCounter = std::numeric_limits<int>::max() - 1;
    int backtraceDepth;
    int sampleRate;
    CaptureOperationMode captureOperationMode;
    bool excludeKernelEvents;
    bool enablePeriodicSampling;
    bool enableOffCpuSampling;

    inline perf_event_group_configurer_config_t(PerfConfig const & perfConfig,
                                                lib::Span<const GatorCpu> clusters,
                                                lib::Span<const int> clusterIds,
                                                bool excludeKernelEvents,
                                                agents::perf::buffer_config_t const & ringbuffer_config,
                                                int64_t schedSwitchId,
                                                int backtraceDepth,
                                                int sampleRate,
                                                CaptureOperationMode captureOperationMode,
                                                bool enablePeriodicSampling,
                                                bool enableOffCpuSampling)
        : perfConfig(perfConfig),
          clusters(clusters),
          clusterIds(clusterIds),
          ringbuffer_config(ringbuffer_config),
          schedSwitchId(schedSwitchId),
          backtraceDepth(backtraceDepth),
          sampleRate(sampleRate),
          captureOperationMode(captureOperationMode),
          excludeKernelEvents(excludeKernelEvents),
          enablePeriodicSampling(enablePeriodicSampling),
          enableOffCpuSampling(enableOffCpuSampling)
    {
    }
};

/** The tuple of attr + gator key representing one event that is part of the capture */
struct perf_event_t {
    struct perf_event_attr attr;
    int key;
};

/** The common state data for the activator and configurer; only this part gets serialized */
struct perf_event_group_configurer_state_t {
    // list of events associated with the group, where the first must be the group leader
    // the list is held externally
    std::vector<perf_event_t> events;
};

/** Like perf_groups_configurer_t, anages the construction / specification of the set of perf event attributes required for some capture */
class perf_event_group_configurer_t {
public:
    perf_event_group_configurer_t(perf_event_group_configurer_config_t & config,
                                  PerfEventGroupIdentifier const & identifier,
                                  perf_event_group_configurer_state_t & state)
        : config(config), identifier(identifier), state(state)
    {
    }

    [[nodiscard]] bool requiresLeader() const
    {
        switch (identifier.getType()) {
            case PerfEventGroupIdentifier::Type::GLOBAL:
            case PerfEventGroupIdentifier::Type::SPECIFIC_CPU:
            case PerfEventGroupIdentifier::Type::SPE:
                return false;
            case PerfEventGroupIdentifier::Type::PER_CLUSTER_CPU_PINNED:
            case PerfEventGroupIdentifier::Type::PER_CLUSTER_CPU_MUXED:
            case PerfEventGroupIdentifier::Type::UNCORE_PMU:
                return true;
            default:
                assert(false && "Unexpected group type");
                return false;
        }
    }

    [[nodiscard]] bool hasLeader() const { return requiresLeader() && (!state.events.empty()); }

    [[nodiscard]] static bool initEvent(perf_event_group_configurer_config_t & config,
                                        perf_event_t & event,
                                        bool is_header,
                                        bool requires_leader,
                                        PerfEventGroupIdentifier::Type type,
                                        bool leader,
                                        attr_to_key_mapping_tracker_t & mapping_tracker,
                                        int key,
                                        const IPerfGroups::Attr & attr,
                                        bool has_aux_data,
                                        bool uses_strobe_period);

    [[nodiscard]] static int nextDummyKey(perf_event_group_configurer_config_t & config);

    [[nodiscard]] bool addEvent(bool leader,
                                attr_to_key_mapping_tracker_t & mapping_tracker,
                                int key,
                                const IPerfGroups::Attr & attr,
                                bool hasAuxData);
    [[nodiscard]] bool createGroupLeader(attr_to_key_mapping_tracker_t & mapping_tracker, bool ebs_metric);

private:
    perf_event_group_configurer_config_t & config;
    PerfEventGroupIdentifier const & identifier;
    perf_event_group_configurer_state_t & state;

    [[nodiscard]] bool createCpuGroupLeaderPinned(attr_to_key_mapping_tracker_t & mapping_tracker);
    [[nodiscard]] bool createCpuGroupLeaderMuxed(attr_to_key_mapping_tracker_t & mapping_tracker, bool ebs_metric);
    [[nodiscard]] bool createUncoreGroupLeader(attr_to_key_mapping_tracker_t & mapping_tracker);
    [[nodiscard]] int nextDummyKey() { return nextDummyKey(config); }
};

#endif /* INCLUDE_LINUX_PERF_PERF_EVENT_GROUP_H */
