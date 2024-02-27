/* Copyright (C) 2023-2024 by Arm Limited. All rights reserved. */

#pragma once

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <string_view>

namespace metrics {
    /** Enumerates which architecture versions the group is specified for */
    enum class metric_arch_t : std::uint8_t {
        any,
        v7,
        v8,
    };

    /** Enumerates possible priority groups */
    enum class metric_priority_t : std::uint8_t {
        backend_bound,
        backend_stalled_cycles,
        backend,
        bad_speculation,
        branch,
        bus,
        cas,
        cpi,
        data,
        frontend_bound,
        frontend_stalled_cycles,
        frontend,
        instruction,
        ipc,
        l2,
        l3,
        ll,
        ls,
        numeric,
        retiring,
    };

    /** Definition of a single metric */
    struct metric_events_set_t {
        std::initializer_list<std::uint16_t> event_codes;
        std::string_view identifier;
        std::string_view title;
        std::string_view description;
        std::string_view unit;
        std::uint16_t instance_no;
        metric_priority_t priority_group;
        metric_arch_t arch;
    };

    /** The list of metrics associated with some CPU */
    using metric_cpu_events_t = std::initializer_list<std::reference_wrapper<metric_events_set_t const>>;

    /** The CPU to metric list entry */
    struct metric_cpu_event_map_entry_t {
        std::reference_wrapper<metric_cpu_events_t const> events;
        std::uint16_t return_event_code;
        std::size_t largest_metric_event_count;
    };

    /** The CPU to metric list lookup type */
    using metric_cpu_events_map_t = std::map<std::string_view, metric_cpu_event_map_entry_t>;

    /**
     * The map from CPU to metric list
     */
    extern metric_cpu_events_map_t const cpu_metrics_table;
}
