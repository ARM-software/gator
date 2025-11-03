/* Copyright (C) 2023-2025 by Arm Limited (or its affiliates). All rights reserved. */

#pragma once

#include "lib/Span.h"
#include "metrics/definitions.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace metrics {
    using score_t = std::uint64_t;

    struct combination_t {
        std::unordered_set<metric_events_set_t const *> contains_sets;
        std::unordered_set<std::uint16_t> event_codes;
        std::uint16_t ebs_ratio;
        metric_arch_t arch = metric_arch_t::any;
        bool uses_cycles;

        combination_t(std::unordered_set<metric_events_set_t const *> contains_sets,
                      std::unordered_set<std::uint16_t> event_codes,
                      std::uint16_t ebs_ratio,
                      metric_arch_t arch,
                      bool uses_cycles)
            : contains_sets(std::move(contains_sets)),
              event_codes(std::move(event_codes)),
              ebs_ratio(ebs_ratio),
              arch(arch),
              uses_cycles(uses_cycles)
        {
        }
    };

    [[nodiscard]] metric_cpu_event_map_entry_t const * find_events_for_cset(std::string_view cset_id);

    [[nodiscard]] std::vector<combination_t> make_combinations(
        bool ebs_mode,
        std::size_t max_events,
        lib::Span<std::reference_wrapper<metrics::metric_events_set_t const> const> events,
        std::function<bool(metric_events_set_t const &)> const & filter_predicate = [](auto const &) { return true; });
}
