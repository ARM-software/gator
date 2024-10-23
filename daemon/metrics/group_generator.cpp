/* Copyright (C) 2023-2024 by Arm Limited. All rights reserved. */

#include "metrics/group_generator.hpp"

#include "lib/Assert.h"
#include "lib/Span.h"
#include "metrics/definitions.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace metrics {
    namespace {
        enum class metric_priority_simplified_t {
            top_level,
            boundness,
            stall_cycles,
            backend,
            branch,
            frontend,
            instruction,
            bus,
            cas,
            data,
            l2,
            l3,
            ll,
            ls,
            numeric,
            barrier,
            latency,
            iq,
        };

        struct raw_combination_t {
            std::unordered_set<metric_events_set_t const *> contains_sets;
            std::unordered_set<std::uint16_t> event_codes;
            metric_priority_simplified_t priority;
            metric_arch_t arch;

            raw_combination_t(std::unordered_set<metric_events_set_t const *> contains_sets,
                              std::unordered_set<std::uint16_t> event_codes,
                              metric_priority_simplified_t priority,
                              metric_arch_t arch)
                : contains_sets(std::move(contains_sets)),
                  event_codes(std::move(event_codes)),
                  priority(priority),
                  arch(arch)
            {
            }
        };

        [[nodiscard]] bool is_cycle_counter(std::uint16_t code, metric_arch_t arch)
        {
            constexpr std::uint16_t arm32_linux_cycle_counter = 0xff;
            constexpr std::uint16_t arm64_linux_cycle_counter = 0x11;

            switch (arch) {
                case metric_arch_t::v7:
                    return (code == arm32_linux_cycle_counter);
                case metric_arch_t::v8:
                    return (code == arm64_linux_cycle_counter);
                case metric_arch_t::any:
                default:
                    return false;
            }
        }

        [[nodiscard]] constexpr metric_priority_simplified_t select_best(metric_priority_simplified_t a,
                                                                         metric_priority_simplified_t b)
        {
            return std::min(a, b);
        }

        [[nodiscard]] constexpr metric_priority_simplified_t normalize_group_0(metric_priority_t prio)
        {
            switch (prio) {
                case metric_priority_t::backend_bound:
                case metric_priority_t::frontend_bound:
                    return metric_priority_simplified_t::boundness;
                case metric_priority_t::backend_stalled_cycles:
                case metric_priority_t::frontend_stalled_cycles:
                    return metric_priority_simplified_t::stall_cycles;
                case metric_priority_t::bad_speculation:
                case metric_priority_t::cpi:
                case metric_priority_t::ipc:
                case metric_priority_t::retiring:
                    return metric_priority_simplified_t::top_level;

                case metric_priority_t::backend:
                    return metric_priority_simplified_t::backend;
                case metric_priority_t::branch:
                    return metric_priority_simplified_t::branch;
                case metric_priority_t::bus:
                    return metric_priority_simplified_t::bus;
                case metric_priority_t::cas:
                    return metric_priority_simplified_t::cas;
                case metric_priority_t::data:
                    return metric_priority_simplified_t::data;
                case metric_priority_t::frontend:
                    return metric_priority_simplified_t::frontend;
                case metric_priority_t::instruction:
                    return metric_priority_simplified_t::instruction;
                case metric_priority_t::l2:
                case metric_priority_t::l2i:
                    return metric_priority_simplified_t::l2;
                case metric_priority_t::l3:
                    return metric_priority_simplified_t::l3;
                case metric_priority_t::ll:
                    return metric_priority_simplified_t::ll;
                case metric_priority_t::ls:
                    return metric_priority_simplified_t::ls;
                case metric_priority_t::numeric:
                    return metric_priority_simplified_t::numeric;
                case metrics::metric_priority_t::barrier:
                    return metric_priority_simplified_t::barrier;
                case metrics::metric_priority_t::latency:
                    return metric_priority_simplified_t::latency;
                case metrics::metric_priority_t::iq:
                    return metric_priority_simplified_t::iq;
                default:
                    throw std::runtime_error("What is this?");
            }
        }

        [[nodiscard]] metric_arch_t combine_arch(metric_arch_t a, metric_arch_t b)
        {
            if (a == metric_arch_t::any) {
                return b;
            }

            if (b == metric_arch_t::any) {
                return b;
            }

            runtime_assert(a == b, "Invalid arch combo");

            return a;
        }

        template<typename EventCodesA, typename EventCodesB>
        [[nodiscard]] std::unordered_set<std::uint16_t> combine_codes(EventCodesA const & event_codes_a,
                                                                      metric_arch_t arch_a,
                                                                      EventCodesB const & event_codes_b,
                                                                      metric_arch_t arch_b)
        {
            std::unordered_set<std::uint16_t> result {};

            for (auto const event : event_codes_a) {
                if (!is_cycle_counter(event, arch_a)) {
                    result.insert(event);
                }
            }

            for (auto const event : event_codes_b) {
                if (!is_cycle_counter(event, arch_b)) {
                    result.insert(event);
                }
            }

            return result;
        }

        template<typename EventCodes>
        [[nodiscard]] std::unordered_set<std::uint16_t> filter_cycles(EventCodes const & event_codes,
                                                                      metric_arch_t arch)
        {
            std::unordered_set<std::uint16_t> result {};

            for (auto const event : event_codes) {
                if (!is_cycle_counter(event, arch)) {
                    result.insert(event);
                }
            }

            return result;
        }

        void make_initial_combinations_inner(
            std::size_t max_events,
            lib::Span<std::reference_wrapper<metrics::metric_events_set_t const> const> const & metric_events,
            std::function<bool(metric_events_set_t const &)> const & filter_predicate,
            bool & has_boundness,
            bool & has_stalled_cycles,
            std::vector<raw_combination_t> & result,
            std::unordered_set<metric_events_set_t const *> & consumed_metrics)
        {
            for (metric_events_set_t const & metric_a : metric_events) {
                // filter metric based on predicate
                if (!filter_predicate(metric_a)) {
                    continue;
                }

                // dont reuse metrics
                if (auto const [it, inserted] = consumed_metrics.insert(&metric_a); !inserted) {
                    (void) it; // GCC7 :-(
                    continue;
                }

                raw_combination_t current_combination {
                    {&metric_a},
                    filter_cycles(metric_a.event_codes, metric_a.arch),
                    normalize_group_0(metric_a.priority_group),
                    metric_a.arch,
                };

                if (current_combination.event_codes.size() > max_events) {
                    continue;
                }

                // update the input flags
                has_boundness |= (current_combination.priority == metric_priority_simplified_t::boundness);
                has_stalled_cycles |= (current_combination.priority == metric_priority_simplified_t::stall_cycles);

                // stick normalized priority items together
                for (metric_events_set_t const & metric_b : metric_events) {
                    // filter metric based on predicate
                    if (!filter_predicate(metric_b)) {
                        continue;
                    }

                    // dont reuse metrics
                    if (consumed_metrics.count(&metric_b) != 0) {
                        continue;
                    }

                    // can it be combined as it is in the same group
                    auto const priority = normalize_group_0(metric_b.priority_group);
                    if (current_combination.priority != priority) {
                        continue;
                    }

                    // combine the event codes
                    auto combined_codes = combine_codes(current_combination.event_codes,
                                                        current_combination.arch,
                                                        metric_b.event_codes,
                                                        metric_b.arch);
                    if (combined_codes.size() > max_events) {
                        continue;
                    }

                    // combine architectures
                    auto const combined_arch = combine_arch(current_combination.arch, metric_b.arch);

                    // update current
                    consumed_metrics.insert(&metric_b);
                    current_combination.contains_sets.insert(&metric_b);
                    current_combination.arch = combined_arch;
                    current_combination.event_codes = std::move(combined_codes);
                }

                result.emplace_back(std::move(current_combination));
            }
        }

        [[nodiscard]] std::vector<raw_combination_t> make_initial_combinations(
            std::size_t max_events,
            lib::Span<std::reference_wrapper<metrics::metric_events_set_t const> const> events,
            std::function<bool(metric_events_set_t const &)> const & filter_predicate,
            bool & has_boundness,
            bool & has_stalled_cycles)
        {
            std::vector<raw_combination_t> result {};

            std::unordered_set<metric_events_set_t const *> consumed_metrics {};

            has_boundness = false;
            has_stalled_cycles = false;

            make_initial_combinations_inner(max_events,
                                            events,
                                            filter_predicate,
                                            has_boundness,
                                            has_stalled_cycles,
                                            result,
                                            consumed_metrics);

            return result;
        }

        [[nodiscard]] bool is_already_consumed(std::unordered_set<metric_events_set_t const *> const & consumed_metrics,
                                               raw_combination_t const & combination)
        {
            for (auto const * set : combination.contains_sets) {
                if (consumed_metrics.count(set) != 0) {
                    return true;
                }
            }
            return false;
        }

        template<typename Predicate>
        [[nodiscard]] std::vector<raw_combination_t> combine_combinations(
            std::size_t max_events,
            std::vector<raw_combination_t> initial_combinations,
            Predicate && predicate)
        {
            static_assert(std::is_invocable_r_v<bool, Predicate, raw_combination_t const &, raw_combination_t const &>);

            // attempt to mush combinations together
            while (true) {
                std::vector<raw_combination_t> new_combinations {};
                std::unordered_set<metric_events_set_t const *> consumed_metrics {};

                bool modified = false;

                for (auto const & combination_a : initial_combinations) {
                    // dont reuse metrics
                    if (is_already_consumed(consumed_metrics, combination_a)) {
                        continue;
                    }

                    if (combination_a.event_codes.size() > max_events) {
                        continue;
                    }

                    // base the new combination of of our starting point
                    raw_combination_t current_combination {combination_a};
                    consumed_metrics.insert(combination_a.contains_sets.begin(), combination_a.contains_sets.end());

                    // attempt to append other combinations to the current combination
                    for (auto const & combination_b : initial_combinations) {
                        // dont reuse metrics
                        if (is_already_consumed(consumed_metrics, combination_b)) {
                            continue;
                        }

                        // check combination
                        if (!predicate(current_combination, combination_b)) {
                            continue;
                        }

                        // combine the event codes
                        auto combined_codes = combine_codes(current_combination.event_codes,
                                                            current_combination.arch,
                                                            combination_b.event_codes,
                                                            combination_b.arch);
                        if (combined_codes.size() > max_events) {
                            continue;
                        }

                        // combine architectures
                        auto const combined_arch = combine_arch(current_combination.arch, combination_b.arch);

                        // update current
                        modified |= combined_codes.size() != current_combination.event_codes.size();
                        consumed_metrics.insert(combination_b.contains_sets.begin(), combination_b.contains_sets.end());
                        current_combination.contains_sets.insert(combination_b.contains_sets.begin(),
                                                                 combination_b.contains_sets.end());
                        current_combination.arch = combined_arch;
                        current_combination.event_codes = std::move(combined_codes);
                        current_combination.priority =
                            select_best(current_combination.priority, combination_b.priority);
                    }

                    new_combinations.emplace_back(std::move(current_combination));
                }

                if (!modified) {
                    return new_combinations;
                }

                initial_combinations = std::move(new_combinations);
            }
        }

        template<metric_priority_simplified_t... Enums>
        [[nodiscard]] constexpr bool is_one_of(metric_priority_simplified_t v)
        {
            return (... || (v == Enums));
        }

        template<metric_priority_simplified_t... Priorities>
        [[nodiscard]] constexpr auto filter_for_priorities()
        {
            return [](raw_combination_t const & a, raw_combination_t const & b) -> bool {
                return a.priority == b.priority
                    || (is_one_of<Priorities...>(a.priority) == is_one_of<Priorities...>(b.priority));
            };
        }

        [[nodiscard]] std::vector<combination_t> convert_to_final(std::vector<raw_combination_t> combinations)
        {
            std::vector<combination_t> result {};
            result.reserve(combinations.size());

            for (auto & combination : combinations) {
                result.emplace_back(
                    std::move(combination.contains_sets),
                    std::set<std::uint16_t>(combination.event_codes.begin(), combination.event_codes.end()),
                    combination.arch);
            }

            return result;
        }
    }

    metric_cpu_event_map_entry_t const * find_events_for_cset(std::string_view cset_id)
    {
        if (auto const it = cpu_metrics_table.find(cset_id); it != cpu_metrics_table.end()) {
            return &(it->second);
        }
        return nullptr;
    }

    std::vector<combination_t> make_combinations(
        std::size_t max_events,
        lib::Span<std::reference_wrapper<metrics::metric_events_set_t const> const> events,
        std::function<bool(metric_events_set_t const &)> const & filter_predicate)
    {
        bool has_boundness = false;
        bool has_stalled_cycles = false;

        // make the initial set
        auto raw_combinations =
            make_initial_combinations(max_events, events, filter_predicate, has_boundness, has_stalled_cycles);

        // merge boundness and top_level if possible
        raw_combinations = combine_combinations(
            max_events,
            std::move(raw_combinations),
            filter_for_priorities<metric_priority_simplified_t::top_level, metric_priority_simplified_t::boundness>());

        // merge branch and top_level if the group has boundness and stalled_cycles (branches are prioritized over stall cycles)
        if (has_boundness && has_stalled_cycles) {
            raw_combinations = combine_combinations(
                max_events,
                std::move(raw_combinations),
                filter_for_priorities<metric_priority_simplified_t::top_level, metric_priority_simplified_t::branch>());
        }

        // merge stalled_cycles and top_level if possible
        raw_combinations = combine_combinations(max_events,
                                                std::move(raw_combinations),
                                                filter_for_priorities<metric_priority_simplified_t::top_level,
                                                                      metric_priority_simplified_t::stall_cycles>());

        // merge branch and top_level if not done previously
        if (!has_boundness || !has_stalled_cycles) {
            raw_combinations = combine_combinations(
                max_events,
                std::move(raw_combinations),
                filter_for_priorities<metric_priority_simplified_t::top_level, metric_priority_simplified_t::branch>());
        }

        // merge boundness, stall_cylces, frontend, backend
        raw_combinations = combine_combinations(max_events,
                                                std::move(raw_combinations),
                                                filter_for_priorities<metric_priority_simplified_t::boundness,
                                                                      metric_priority_simplified_t::stall_cycles,
                                                                      metric_priority_simplified_t::frontend,
                                                                      metric_priority_simplified_t::backend>());

        // merge data and top_level
        raw_combinations = combine_combinations(
            max_events,
            std::move(raw_combinations),
            filter_for_priorities<metric_priority_simplified_t::top_level, metric_priority_simplified_t::data>());

        // merge data and ls
        raw_combinations = combine_combinations(
            max_events,
            std::move(raw_combinations),
            filter_for_priorities<metric_priority_simplified_t::data, metric_priority_simplified_t::ls>());

        // merge data, ls, l2
        raw_combinations = combine_combinations(max_events,
                                                std::move(raw_combinations),
                                                filter_for_priorities<metric_priority_simplified_t::data,
                                                                      metric_priority_simplified_t::ls,
                                                                      metric_priority_simplified_t::l2>());

        // merge data, ls, l2, l3
        raw_combinations = combine_combinations(max_events,
                                                std::move(raw_combinations),
                                                filter_for_priorities<metric_priority_simplified_t::data,
                                                                      metric_priority_simplified_t::ls,
                                                                      metric_priority_simplified_t::l2,
                                                                      metric_priority_simplified_t::l3>());

        // merge data, ls, l2, l3, ll
        raw_combinations = combine_combinations(max_events,
                                                std::move(raw_combinations),
                                                filter_for_priorities<metric_priority_simplified_t::data,
                                                                      metric_priority_simplified_t::ls,
                                                                      metric_priority_simplified_t::l2,
                                                                      metric_priority_simplified_t::l3,
                                                                      metric_priority_simplified_t::ll>());

        // merge anything else that will fit together
        raw_combinations =
            combine_combinations(max_events,
                                 std::move(raw_combinations),
                                 [](raw_combination_t const & /*a*/, raw_combination_t const & /*b*/) { return true; });

        return convert_to_final(std::move(raw_combinations));
    }
}
