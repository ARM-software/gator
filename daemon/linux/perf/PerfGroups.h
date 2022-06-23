/* Copyright (C) 2013-2022 by Arm Limited. All rights reserved. */

#ifndef PERF_GROUPS_H
#define PERF_GROUPS_H

#include "k/perf_event.h"
#include "linux/perf/IPerfGroups.h"
#include "linux/perf/PerfEventGroup.h"
#include "linux/perf/PerfEventGroupIdentifier.h"
#include "linux/perf/attr_to_key_mapping_tracker.h"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

class GatorCpu;

/** The part of the state data that gets serialized */
struct perf_groups_configurer_state_t {
    perf_event_t header {};
    std::map<PerfEventGroupIdentifier, perf_event_group_configurer_state_t> perfEventGroupMap {};
    std::size_t numberOfEventsAdded {0};
};

/** Manages the construction / specification of the set of perf event attributes required for some capture */
class perf_groups_configurer_t : public IPerfGroups {
public:
    perf_groups_configurer_t(attr_to_key_mapping_tracker_t & mapping_tracker,
                             perf_event_group_configurer_config_t & configuration,
                             perf_groups_configurer_state_t & state)
        : configuration(configuration), state(state)
    {
        initHeader(mapping_tracker);
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

    void initHeader(attr_to_key_mapping_tracker_t & mapping_tracker);
};

#endif // PERF_GROUPS_H
