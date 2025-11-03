/* Copyright (C) 2023-2025 by Arm Limited (or its affiliates). All rights reserved. */

#include "metrics/group_generator.hpp"

#include "lib/Assert.h"
#include "lib/Span.h"
#include "metrics/definitions.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace metrics {
    namespace {

        struct raw_combination_t {
            std::unordered_set<metric_events_set_t const *> contains_sets;
            std::unordered_set<std::uint16_t> event_codes;
            metric_priority_t priority;
            std::uint16_t ebs_ratio;
            metric_arch_t arch;
            bool uses_cycles;

            raw_combination_t(std::unordered_set<metric_events_set_t const *> contains_sets,
                              std::unordered_set<std::uint16_t> event_codes,
                              metric_priority_t priority,
                              std::uint16_t ebs_ratio,
                              metric_arch_t arch,
                              bool uses_cycles)
                : contains_sets(std::move(contains_sets)),
                  event_codes(std::move(event_codes)),
                  priority(priority),
                  ebs_ratio(ebs_ratio),
                  arch(arch),
                  uses_cycles(uses_cycles)
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

        [[nodiscard]] constexpr metric_priority_t select_best(metric_priority_t a, metric_priority_t b)
        {
            return std::min(a, b);
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

        [[nodiscard]] constexpr std::uint16_t to_event_code(std::uint16_t event)
        {
            return event;
        }

        [[nodiscard]] constexpr std::uint16_t to_event_code(metric_event_code_and_ratio_t const & event)
        {
            return event.code;
        }

        template<typename EventCodesA, typename EventCodesB>
        [[nodiscard]] std::unordered_set<std::uint16_t> combine_codes(EventCodesA const & event_codes_a,
                                                                      metric_arch_t arch_a,
                                                                      EventCodesB const & event_codes_b,
                                                                      metric_arch_t arch_b)
        {
            std::unordered_set<std::uint16_t> result {};

            for (auto const & event : event_codes_a) {
                auto const code = to_event_code(event);
                if (!is_cycle_counter(code, arch_a)) {
                    result.insert(code);
                }
            }

            for (auto const & event : event_codes_b) {
                auto const code = to_event_code(event);
                if (!is_cycle_counter(code, arch_b)) {
                    result.insert(code);
                }
            }

            return result;
        }

        template<typename EventCodes>
        [[nodiscard]] std::unordered_set<std::uint16_t> filter_cycles(EventCodes const & event_codes,
                                                                      metric_arch_t arch)
        {
            std::unordered_set<std::uint16_t> result {};

            for (auto const & event : event_codes) {
                if (!is_cycle_counter(event.code, arch)) {
                    result.insert(event.code);
                }
            }

            return result;
        }

        inline constexpr std::uint16_t EBS_RATE_CYCLES = 1;

        template<typename EventCodes>
        [[nodiscard]] std::uint16_t max_ebs_ratio(bool uses_cycles, EventCodes const & event_codes)
        {
            // if cycles is sampled, then just sample at period=`rate` since that is a high frequency event
            if (uses_cycles) {
                return EBS_RATE_CYCLES;
            }

            // otherwise find the largest ebs_ratio value which causes all events in the group
            // to be sampled with a period=`rate / ebs_ratio`, or in otherwords we want the highest
            // sampling rate of all events in the group to get the best coverage for low-frequency events
            std::uint16_t result = EBS_RATE_CYCLES;

            for (auto const & event : event_codes) {
                result = std::max(result, event.ebs_ratio);
            }

            return result;
        }

        [[nodiscard]] bool is_valid_ebs_combo(std::uint16_t rate_a,
                                              std::uint16_t rate_b,
                                              bool uses_cycles_a,
                                              bool uses_cycles_b)
        {
            // the rates must match
            if (rate_a != rate_b) {
                return false;
            }

            // if the rate is EBS_RATE_CYCLES it can be combined regardless of uses_cycles value
            if (rate_a <= EBS_RATE_CYCLES) {
                return true;
            }

            // uses cycles value must also match
            return uses_cycles_a == uses_cycles_b;
        }

        void make_initial_combinations_inner(
            bool ebs_mode,
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
                    metric_a.priority_group,
                    (ebs_mode ? max_ebs_ratio(metric_a.uses_cycles, metric_a.event_codes) : std::uint16_t(0)),
                    metric_a.arch,
                    metric_a.uses_cycles,
                };

                if (current_combination.event_codes.size() > max_events) {
                    continue;
                }

                // update the input flags
                has_boundness |= (current_combination.priority == metric_priority_t::boundness);
                has_stalled_cycles |= (current_combination.priority == metric_priority_t::stall_cycles);

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
                    if (current_combination.priority != metric_b.priority_group) {
                        continue;
                    }

                    // they should have the same ebs_ratio value since that controls the sample rate
                    if (ebs_mode
                        && !is_valid_ebs_combo(max_ebs_ratio(metric_b.uses_cycles, metric_b.event_codes),
                                               current_combination.ebs_ratio,
                                               metric_b.uses_cycles,
                                               current_combination.uses_cycles)) {
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
            bool ebs_mode,
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

            make_initial_combinations_inner(ebs_mode,
                                            max_events,
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
            bool ebs_mode,
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

                        // they should have the same ebs_ratio value since that controls the sample rate
                        if (ebs_mode
                            && !is_valid_ebs_combo(combination_b.ebs_ratio,
                                                   current_combination.ebs_ratio,
                                                   combination_b.uses_cycles,
                                                   current_combination.uses_cycles)) {
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

        template<metric_priority_t... Enums>
        [[nodiscard]] constexpr bool is_one_of(metric_priority_t v)
        {
            return (... || (v == Enums));
        }

        template<metric_priority_t... Priorities>
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
                result.emplace_back(std::move(combination.contains_sets),
                                    std::move(combination.event_codes),
                                    combination.ebs_ratio,
                                    combination.arch,
                                    combination.uses_cycles);
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
        bool ebs_mode,
        std::size_t max_events,
        lib::Span<std::reference_wrapper<metrics::metric_events_set_t const> const> events,
        std::function<bool(metric_events_set_t const &)> const & filter_predicate)
    {
        bool has_boundness = false;
        bool has_stalled_cycles = false;

        // make the initial set
        auto raw_combinations = make_initial_combinations(ebs_mode,
                                                          max_events,
                                                          events,
                                                          filter_predicate,
                                                          has_boundness,
                                                          has_stalled_cycles);

        // merge boundness and top_level if possible
        raw_combinations =
            combine_combinations(ebs_mode,
                                 max_events,
                                 std::move(raw_combinations),
                                 filter_for_priorities<metric_priority_t::top_level, metric_priority_t::boundness>());

        // merge branch and top_level if the group has boundness and stalled_cycles (branches are prioritized over stall cycles)
        if (has_boundness && has_stalled_cycles) {
            raw_combinations =
                combine_combinations(ebs_mode,
                                     max_events,
                                     std::move(raw_combinations),
                                     filter_for_priorities<metric_priority_t::top_level, metric_priority_t::branch>());
        }

        // merge stalled_cycles and top_level if possible
        raw_combinations = combine_combinations(
            ebs_mode,
            max_events,
            std::move(raw_combinations),
            filter_for_priorities<metric_priority_t::top_level, metric_priority_t::stall_cycles>());

        // merge branch and top_level if not done previously
        if (!has_boundness || !has_stalled_cycles) {
            raw_combinations =
                combine_combinations(ebs_mode,
                                     max_events,
                                     std::move(raw_combinations),
                                     filter_for_priorities<metric_priority_t::top_level, metric_priority_t::branch>());
        }

        // merge boundness, stall_cylces, frontend, backend
        raw_combinations = combine_combinations(ebs_mode,
                                                max_events,
                                                std::move(raw_combinations),
                                                filter_for_priorities<metric_priority_t::boundness,
                                                                      metric_priority_t::stall_cycles,
                                                                      metric_priority_t::frontend,
                                                                      metric_priority_t::backend>());

        // merge data and top_level
        raw_combinations =
            combine_combinations(ebs_mode,
                                 max_events,
                                 std::move(raw_combinations),
                                 filter_for_priorities<metric_priority_t::top_level, metric_priority_t::data>());

        // merge data and ls
        raw_combinations =
            combine_combinations(ebs_mode,
                                 max_events,
                                 std::move(raw_combinations),
                                 filter_for_priorities<metric_priority_t::data, metric_priority_t::ls>());

        // merge data, ls, l2
        raw_combinations = combine_combinations(
            ebs_mode,
            max_events,
            std::move(raw_combinations),
            filter_for_priorities<metric_priority_t::data, metric_priority_t::ls, metric_priority_t::l2>());

        // merge data, ls, l2, l3
        raw_combinations = combine_combinations(ebs_mode,
                                                max_events,
                                                std::move(raw_combinations),
                                                filter_for_priorities<metric_priority_t::data,
                                                                      metric_priority_t::ls,
                                                                      metric_priority_t::l2,
                                                                      metric_priority_t::l3>());

        // merge data, ls, l2, l3, ll
        raw_combinations = combine_combinations(ebs_mode,
                                                max_events,
                                                std::move(raw_combinations),
                                                filter_for_priorities<metric_priority_t::data,
                                                                      metric_priority_t::ls,
                                                                      metric_priority_t::l2,
                                                                      metric_priority_t::l3,
                                                                      metric_priority_t::ll>());

        // merge anything else that will fit together
        raw_combinations =
            combine_combinations(ebs_mode,
                                 max_events,
                                 std::move(raw_combinations),
                                 [](raw_combination_t const & /*a*/, raw_combination_t const & /*b*/) { return true; });

        return convert_to_final(std::move(raw_combinations));
    }
}
