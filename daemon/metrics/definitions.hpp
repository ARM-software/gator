/* Copyright (C) 2023-2025 by Arm Limited (or its affiliates). All rights reserved. */

#pragma once

#include "lib/Assert.h"

#include <cassert>
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
    enum class metric_priority_t {
        top_level,
        boundness,
        stall_cycles,
        backend,
        branch,
        bus,
        cas,
        data,
        frontend,
        instruction,
        l2,
        l3,
        ll,
        ls,
        mem,
        numeric,
        cme,
    };

    enum class metric_group_id_t : std::uint8_t {
        atomics_effectiveness,
        average_latency,
        basic,
        branch_effectiveness,
        bus_effectiveness,
        cme_bus_effectiveness,
        cme_general,
        cme_iq_effectiveness,
        cme_l1d_cache_effectiveness,
        cme_l3d_cache_effectiveness,
        cme_ll_cache_effectiveness,
        cme_miss_ratio,
        cme_mpki,
        cme_operation_mix,
        cme_port_utilization,
        cme_prefetcher_effectiveness,
        cme_system_memory_effectiveness,
        cme_topdown_backend,
        cme_topdown_frontend,
        cme_topdown_l1,
        cycle_accounting,
        dtlb_effectiveness,
        fp_arithmetic_intensity,
        fp_precision_mix,
        general,
        iq_effectiveness,
        itlb_effectiveness,
        l1d_cache_effectiveness,
        l1i_cache_effectiveness,
        l2_cache_effectiveness,
        l2d_cache_effectiveness,
        l2i_cache_effectiveness,
        l3_cache_effectiveness,
        ll_cache_effectiveness,
        mcq_effectiveness,
        miss_ratio,
        mpki,
        operation_mix,
        other,
        port_utilization,
        prefetcher_effectiveness,
        rename_effectiveness,
        sve_effectiveness,
        system_memory_effectiveness,
        topdown_backend,
        topdown_cme,
        topdown_frontend,
        topdown_l1,

        // iteration helpers
        count,
        begin = 0,
        end = count
    };

    constexpr std::string_view metric_group_id_to_string(metric_group_id_t group)
    {
        switch (group) {
            case metric_group_id_t::atomics_effectiveness:
                return "atomics_effectiveness";
            case metric_group_id_t::average_latency:
                return "average_latency";
            case metric_group_id_t::basic:
                return "basic";
            case metric_group_id_t::branch_effectiveness:
                return "branch_effectiveness";
            case metric_group_id_t::bus_effectiveness:
                return "bus_effectiveness";
            case metric_group_id_t::cme_bus_effectiveness:
                return "cme_bus_effectiveness";
            case metric_group_id_t::cme_general:
                return "cme_general";
            case metric_group_id_t::cme_iq_effectiveness:
                return "cme_iq_effectiveness";
            case metric_group_id_t::cme_l1d_cache_effectiveness:
                return "cme_l1d_cache_effectiveness";
            case metric_group_id_t::cme_l3d_cache_effectiveness:
                return "cme_l3d_cache_effectiveness";
            case metric_group_id_t::cme_ll_cache_effectiveness:
                return "cme_ll_cache_effectiveness";
            case metric_group_id_t::cme_miss_ratio:
                return "cme_miss_ratio";
            case metric_group_id_t::cme_mpki:
                return "cme_mpki";
            case metric_group_id_t::cme_operation_mix:
                return "cme_operation_mix";
            case metric_group_id_t::cme_port_utilization:
                return "cme_port_utilization";
            case metric_group_id_t::cme_prefetcher_effectiveness:
                return "cme_prefetcher_effectiveness";
            case metric_group_id_t::cme_system_memory_effectiveness:
                return "cme_system_memory_effectiveness";
            case metric_group_id_t::cme_topdown_backend:
                return "cme_topdown_backend";
            case metric_group_id_t::cme_topdown_frontend:
                return "cme_topdown_frontend";
            case metric_group_id_t::cme_topdown_l1:
                return "cme_topdown_l1";
            case metric_group_id_t::cycle_accounting:
                return "cycle_accounting";
            case metric_group_id_t::dtlb_effectiveness:
                return "dtlb_effectiveness";
            case metric_group_id_t::fp_arithmetic_intensity:
                return "fp_arithmetic_intensity";
            case metric_group_id_t::fp_precision_mix:
                return "fp_precision_mix";
            case metric_group_id_t::general:
                return "general";
            case metric_group_id_t::iq_effectiveness:
                return "iq_effectiveness";
            case metric_group_id_t::itlb_effectiveness:
                return "itlb_effectiveness";
            case metric_group_id_t::l1d_cache_effectiveness:
                return "l1d_cache_effectiveness";
            case metric_group_id_t::l1i_cache_effectiveness:
                return "l1i_cache_effectiveness";
            case metric_group_id_t::l2_cache_effectiveness:
                return "l2_cache_effectiveness";
            case metric_group_id_t::l2d_cache_effectiveness:
                return "l2d_cache_effectiveness";
            case metric_group_id_t::l2i_cache_effectiveness:
                return "l2i_cache_effectiveness";
            case metric_group_id_t::l3_cache_effectiveness:
                return "l3_cache_effectiveness";
            case metric_group_id_t::ll_cache_effectiveness:
                return "ll_cache_effectiveness";
            case metric_group_id_t::mcq_effectiveness:
                return "mcq_effectiveness";
            case metric_group_id_t::miss_ratio:
                return "miss_ratio";
            case metric_group_id_t::mpki:
                return "mpki";
            case metric_group_id_t::operation_mix:
                return "operation_mix";
            case metric_group_id_t::other:
                return "other";
            case metric_group_id_t::port_utilization:
                return "port_utilization";
            case metric_group_id_t::prefetcher_effectiveness:
                return "prefetcher_effectiveness";
            case metric_group_id_t::rename_effectiveness:
                return "rename_effectiveness";
            case metric_group_id_t::sve_effectiveness:
                return "sve_effectiveness";
            case metric_group_id_t::system_memory_effectiveness:
                return "system_memory_effectiveness";
            case metric_group_id_t::topdown_backend:
                return "topdown_backend";
            case metric_group_id_t::topdown_cme:
                return "topdown_cme";
            case metric_group_id_t::topdown_frontend:
                return "topdown_frontend";
            case metric_group_id_t::topdown_l1:
                return "topdown_l1";

            case metric_group_id_t::count:
            default:
                runtime_assert(false, "Invalid metric group ID was provided");
        }
        return "";
    }

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

    /** Represents a single entry in the hierarchy */
    struct metric_hierarchy_entry_t {
        std::reference_wrapper<metric_events_set_t const> metric;
        std::initializer_list<metric_hierarchy_entry_t> children;
        metric_group_id_t group;
        bool top_down;
    };

    /** The list of metrics associated with some CPU */
    using metric_cpu_events_t = std::initializer_list<metric_hierarchy_entry_t>;

    /** Properties pertaining to on version of a cpu */
    struct metric_cpu_version_map_entry_t {
        std::reference_wrapper<metric_cpu_events_t const> root_events;
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

    /**
     * Map group title from enum
     */
    [[nodiscard]] extern std::string_view metric_group_title(metric_group_id_t id);
}
