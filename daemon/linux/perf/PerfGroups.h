/* Copyright (C) 2013-2022 by Arm Limited. All rights reserved. */

#ifndef PERF_GROUPS_H
#define PERF_GROUPS_H

#include "linux/perf/IPerfGroups.h"
#include "linux/perf/PerfEventGroup.h"
#include "linux/perf/PerfEventGroupIdentifier.h"
#include "linux/perf/attr_to_key_mapping_tracker.h"
#include "linux/perf/id_to_key_mapping_tracker.h"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

class GatorCpu;

/** The part of the state data that gets serialized */
template<typename StateType>
struct perf_groups_common_serialized_state_t {
    std::map<PerfEventGroupIdentifier, StateType> perfEventGroupMap {};
    std::size_t numberOfEventsAdded {0};

    /** Helper for converting once state type to another */
    template<typename FromStateType>
    static inline perf_groups_common_serialized_state_t<StateType> convert_from(
        perf_groups_common_serialized_state_t<FromStateType> && from_state)
    {
        perf_groups_common_serialized_state_t<StateType> result {};

        for (auto & entry : from_state.perfEventGroupMap) {
            result.perfEventGroupMap.emplace(entry.first, StateType {std::move(entry.second.common)});
        }

        result.numberOfEventsAdded = from_state.numberOfEventsAdded;

        return result;
    }
};

using perf_groups_activator_state_t = perf_groups_common_serialized_state_t<perf_event_group_activator_state_t>;
using perf_groups_configurer_state_t = perf_groups_common_serialized_state_t<perf_event_group_configurer_state_t>;

/** Manages the construction / specification of the set of perf event attributes required for some capture */
class perf_groups_configurer_t : public IPerfGroups {
public:
    perf_groups_configurer_t(perf_event_group_configurer_config_t & configuration,
                             perf_groups_configurer_state_t & state)
        : configuration(configuration), state(state)
    {
    }

    bool add(attr_to_key_mapping_tracker_t & mapping_tracker,
             const PerfEventGroupIdentifier & groupIdentifier,
             int key,
             const Attr & attr,
             bool hasAuxData = false) override;

    void addGroupLeader(attr_to_key_mapping_tracker_t & mapping_tracker,
                        const PerfEventGroupIdentifier & groupIdentifier) override
    {
        getGroup(mapping_tracker, groupIdentifier);
    }

private:
    perf_event_group_configurer_config_t & configuration;
    perf_groups_configurer_state_t & state;

    /// Get the group and create the group leader if needed
    perf_event_group_configurer_t getGroup(attr_to_key_mapping_tracker_t & mapping_tracker,
                                           const PerfEventGroupIdentifier & groupIdentifier);
};

/** Provides the necessary functionality to activate the perf event attributes for some capture */
class perf_groups_activator_t {
public:
    static std::size_t getMaxFileDescriptors();

    perf_groups_activator_t(perf_event_group_activator_config_t const & configuration,
                            perf_groups_activator_state_t & state,
                            std::size_t maxFiles = getMaxFileDescriptors())
        : configuration(configuration), state(state), maxFiles(maxFiles)
    {
    }

    [[nodiscard]] bool hasSPE() const;

    /**
     * @param appPids ignored if system wide
     * @note Not safe to call concurrently.
     */
    std::pair<OnlineResult, std::string> onlineCPU(int cpu,
                                                   const std::set<int> & appPids,
                                                   OnlineEnabledState enabledState,
                                                   id_to_key_mapping_tracker_t & mapping_tracker,
                                                   const std::function<bool(int)> & addToMonitor,
                                                   const std::function<bool(int, int, bool)> & addToBuffer,
                                                   const std::function<std::set<int>(int)> & childTids);

    bool offlineCPU(int cpu, const std::function<void(int)> & removeFromBuffer);
    void start();
    void stop();

private:
    perf_event_group_activator_config_t configuration;
    perf_groups_activator_state_t & state;
    std::map<int, unsigned int> eventsOpenedPerCpu {};
    std::size_t maxFiles;
};

#endif // PERF_GROUPS_H
