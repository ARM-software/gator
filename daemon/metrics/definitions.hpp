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
        barrier,
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
        iq,
        l2,
        l2i,
        l3,
        latency,
        ll,
        ls,
        numeric,
        retiring,
    };

    enum class metric_group_id_t : std::uint8_t {
        atomics_effectiveness,
        average_latency,
        basic,
        branch_effectiveness,
        bus_effectiveness,
        cycle_accounting,
        dtlb_effectiveness,
        fp_arithmetic_intensity,
        fp_precision_mix,
        general,
        itlb_effectiveness,
        l1d_cache_effectiveness,
        l1i_cache_effectiveness,
        l2_cache_effectiveness,
        l2d_cache_effectiveness,
        l2i_cache_effectiveness,
        l3_cache_effectiveness,
        ll_cache_effectiveness,
        miss_ratio,
        mpki,
        operation_mix,
        topdown_backend,
        topdown_frontend,
        topdown_l1,
        iq_effectiveness,
        mcq_effectiveness,
        sve_effectiveness,
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
        std::initializer_list<metric_group_id_t> groups;
    };

    /** The list of metrics associated with some CPU */
    using metric_cpu_events_t = std::initializer_list<std::reference_wrapper<metric_events_set_t const>>;

    /** Properties pertaining to on version of a cpu */
    struct metric_cpu_version_map_entry_t {
        std::reference_wrapper<metric_cpu_events_t const> events;
        std::size_t largest_metric_event_count;
    };

    struct metric_cpu_version_t {
        static constexpr unsigned major_common_flag = ~0U;

        unsigned major_version;
        unsigned minor_version;

        constexpr metric_cpu_version_t(unsigned major_version = major_common_flag, unsigned minor_version = 0)
            : major_version(major_version), minor_version(minor_version)
        {
        }

        [[nodiscard]] constexpr bool is_common() const { return major_version == major_common_flag; }

        [[nodiscard]] constexpr friend auto operator==(metric_cpu_version_t const & a, metric_cpu_version_t const & b)
        {
            if (a.is_common() == b.is_common()) {
                return true;
            }

            return (a.major_version == b.major_version) && (a.minor_version == b.minor_version);
        }

        [[nodiscard]] constexpr friend auto operator!=(metric_cpu_version_t const & a, metric_cpu_version_t const & b)
        {
            return !(a == b);
        }

        [[nodiscard]] constexpr friend auto operator<(metric_cpu_version_t const & a, metric_cpu_version_t const & b)
        {
            // sort common first
            if (a.is_common()) {
                return !b.is_common();
            }
            if (b.is_common()) {
                return false;
            }

            // then by major
            if (a.major_version < b.major_version) {
                return true;
            }
            if (b.major_version > a.major_version) {
                return false;
            }

            // then by minor
            return a.minor_version < b.minor_version;
        }
    };

    /** The CPU to metric list entry */
    struct metric_cpu_event_map_entry_t {
        std::uint16_t return_event_code;
        std::map<metric_cpu_version_t, metric_cpu_version_map_entry_t> per_version_metrics;
    };

    /** The CPU to metric list lookup type */
    using metric_cpu_events_map_t = std::map<std::string_view, metric_cpu_event_map_entry_t>;

    /**
     * The map from CPU to metric list
     */
    extern metric_cpu_events_map_t const cpu_metrics_table;
}
