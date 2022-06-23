/* Copyright (C) 2018-2022 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_PERF_PERF_EVENT_GROUP_H
#define INCLUDE_LINUX_PERF_PERF_EVENT_GROUP_H

#include "Tracepoints.h"
#include "k/perf_event.h" // Use a snapshot of perf_event.h as it may be more recent than what is on the target and if not newer features won't be supported anyways
#include "lib/AutoClosingFd.h"
#include "lib/Span.h"
#include "linux/perf/IPerfGroups.h"
#include "linux/perf/PerfBuffer.h"
#include "linux/perf/PerfConfig.h"
#include "linux/perf/PerfEventGroupIdentifier.h"
#include "linux/perf/attr_to_key_mapping_tracker.h"
#include "linux/perf/id_to_key_mapping_tracker.h"

#include <climits>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <vector>

class IPerfmapping_tracker;
class GatorCpu;

enum class OnlineResult {
    SUCCESS,
    FAILURE,
    CPU_OFFLINE,
    OTHER_FAILURE,
};

enum class OnlineEnabledState { ENABLE_NOW, ENABLE_ON_EXEC, NOT_ENABLED };

/** Configuration common to both the activator and configurer */
struct perf_event_group_common_config_t {
    const PerfConfig & perfConfig;
    lib::Span<const GatorCpu> clusters;
    lib::Span<const int> clusterIds;
    bool excludeKernelEvents;
};

/** State used by the configurator class */
struct perf_event_group_configurer_config_t : perf_event_group_common_config_t {
    inline perf_event_group_configurer_config_t(PerfConfig const & perfConfig,
                                                lib::Span<const GatorCpu> clusters,
                                                lib::Span<const int> clusterIds,
                                                bool excludeKernelEvents,
                                                perf_ringbuffer_config_t const & ringbuffer_config,
                                                int64_t schedSwitchId,
                                                int backtraceDepth,
                                                int sampleRate,
                                                bool enablePeriodicSampling)
        : perf_event_group_common_config_t {perfConfig, clusters, clusterIds, excludeKernelEvents},
          ringbuffer_config(ringbuffer_config),
          schedSwitchId(schedSwitchId),
          backtraceDepth(backtraceDepth),
          sampleRate(sampleRate),
          enablePeriodicSampling(enablePeriodicSampling)
    {
    }

    perf_ringbuffer_config_t ringbuffer_config;
    /// tracepoint ID for sched_switch or UNKNOWN_TRACE_POINT_ID
    int64_t schedSwitchId;
    int schedSwitchKey = std::numeric_limits<int>::max();
    int dummyKeyCounter = std::numeric_limits<int>::max() - 1;
    int backtraceDepth;
    int sampleRate;
    bool enablePeriodicSampling;
};

/** State used by the activator class */
using perf_event_group_activator_config_t = perf_event_group_common_config_t;

/** The tuple of attr + gator key representing one event that is part of the capture */
struct perf_event_t {
    struct perf_event_attr attr;
    int key;
};

/** The common state data for the activator and configurer; only this part gets serialized */
struct perf_event_group_common_state_t {
    // list of events associated with the group, where the first must be the group leader
    // the list is held externally
    std::vector<perf_event_t> events {};
};

/** The state data specific to the configurer */
struct perf_event_group_configurer_state_t {
    //just the common state, wrapped to make it the same as the activator state
    perf_event_group_common_state_t common {};
};

/** The state data specific to the activator */
struct perf_event_group_activator_state_t {
    // the common state
    perf_event_group_common_state_t common {};
    // map from cpu -> (map from mEvents index -> (map from tid -> file descriptor))
    std::map<int, std::map<int, std::map<int, lib::AutoClosingFd>>> cpuToEventIndexToTidToFdMap {};

    perf_event_group_activator_state_t() = default;

    // allow construction from just the serialized part
    explicit perf_event_group_activator_state_t(perf_event_group_common_state_t && common) : common(std::move(common))
    {
    }
};

/** Common base for both PerfEventGroup and PerfEventGroupActivator */
template<typename ConfigType, typename StateType>
class perf_event_group_base_t {
public:
    perf_event_group_base_t(ConfigType & config, PerfEventGroupIdentifier const & identifier, StateType & state)
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
            case PerfEventGroupIdentifier::Type::PER_CLUSTER_CPU:
            case PerfEventGroupIdentifier::Type::UNCORE_PMU:
                return true;
            default:
                assert(false && "Unexpected group type");
                return false;
        }
    }

    [[nodiscard]] bool hasLeader() const { return requiresLeader() && (!state.common.events.empty()); }

protected:
    ConfigType & config;
    PerfEventGroupIdentifier const & identifier;
    StateType & state;
};

/** Like perf_groups_configurer_t, anages the construction / specification of the set of perf event attributes required for some capture */
class perf_event_group_configurer_t
    : private perf_event_group_base_t<perf_event_group_configurer_config_t, perf_event_group_configurer_state_t> {
public:
    perf_event_group_configurer_t(perf_event_group_configurer_config_t & config,
                                  PerfEventGroupIdentifier const & identifier,
                                  perf_event_group_configurer_state_t & state)
        : perf_event_group_base_t(config, identifier, state)
    {
    }

    using perf_event_group_base_t::hasLeader;
    using perf_event_group_base_t::requiresLeader;

    [[nodiscard]] bool addEvent(bool leader,
                                attr_to_key_mapping_tracker_t & mapping_tracker,
                                int key,
                                const IPerfGroups::Attr & attr,
                                bool hasAuxData);
    [[nodiscard]] bool createGroupLeader(attr_to_key_mapping_tracker_t & mapping_tracker);

private:
    [[nodiscard]] bool createCpuGroupLeader(attr_to_key_mapping_tracker_t & mapping_tracker);
    [[nodiscard]] bool createUncoreGroupLeader(attr_to_key_mapping_tracker_t & mapping_tracker);
    [[nodiscard]] int nextDummyKey();
};

/** Like perf_groups_activator_t, provides the necessary functionality to activate the perf event attributes for some capture */
class perf_event_group_activator_t
    : private perf_event_group_base_t<perf_event_group_common_config_t, perf_event_group_activator_state_t> {
public:
    perf_event_group_activator_t(perf_event_group_activator_config_t & config,
                                 PerfEventGroupIdentifier const & identifier,
                                 perf_event_group_activator_state_t & state)
        : perf_event_group_base_t(config, identifier, state)
    {
    }

    std::pair<OnlineResult, std::string> onlineCPU(int cpu,
                                                   std::set<int> & tids,
                                                   OnlineEnabledState enabledState,
                                                   id_to_key_mapping_tracker_t & mapping_tracker,
                                                   const std::function<bool(int)> & addToMonitor,
                                                   const std::function<bool(int, int, bool)> & addToBuffer);

    bool offlineCPU(int cpu);
    void start();
    void stop();

private:
    bool enable(const std::map<int, std::map<int, lib::AutoClosingFd>> & eventIndexToTidToFdMap);
    bool checkEnabled(const std::map<int, std::map<int, lib::AutoClosingFd>> & eventIndexToTidToFdMap);
};

#endif /* INCLUDE_LINUX_PERF_PERF_EVENT_GROUP_H */
