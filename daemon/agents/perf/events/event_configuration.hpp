/* Copyright (C) 2021-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "agents/perf/events/types.hpp"
#include "k/perf_event.h"

#include <cstdint>
#include <map>
#include <vector>

namespace agents::perf {

    /**
     * Defines a single perf event's properties for an event that should be captured
     */
    struct event_definition_t {
        perf_event_attr attr;
        gator_key_t key;
    };

    /**
     * Defines the active capture configuration for the perf capture service
     */
    struct event_configuration_t {
        /** An empty (dummy) event used for the output event for a per-cpu mmap */
        event_definition_t header_event;
        /** The set of events that should be selected globally (i.e. on every active CPU, regardless of CPU type) */
        std::vector<event_definition_t> global_events;
        /** The SPE events, defining the events that may be activated for every CPU that supports SPE */
        std::map<cpu_cluster_id_t, std::vector<event_definition_t>> spe_events;
        /** The map from cluster index to set of events, defining the events that may be activated for any CPU matching a given type */
        std::map<cpu_cluster_id_t, std::map<std::uint32_t, std::vector<event_definition_t>>> cluster_specific_events;
        /** The map from uncore pmu index to set of events, defining the events that may be activated for that uncore */
        std::map<uncore_pmu_id_t, std::vector<event_definition_t>> uncore_specific_events;
        /** The map of CPU specific events, defining the events that may be activated for a specific CPU */
        std::map<core_no_t, std::vector<event_definition_t>> cpu_specific_events;
    };
}
