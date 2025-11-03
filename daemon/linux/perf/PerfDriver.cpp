/* Copyright (C) 2013-2025 by Arm Limited (or its affiliates). All rights reserved. */

#include "linux/perf/PerfDriver.h"

#include "CapturedSpe.h"
#include "Configuration.h"
#include "ConfigurationXML.h"
#include "Counter.h"
#include "DriverCounter.h"
#include "EventCode.h"
#include "GetEventKey.h"
#include "ICpuInfo.h"
#include "Logging.h"
#include "SessionData.h"
#include "SimpleDriver.h"
#include "agents/perf/capture_configuration.h"
#include "k/perf_event.h"
#include "lib/Assert.h"
#include "lib/Format.h"
#include "lib/Span.h"
#include "lib/String.h"
#include "lib/Utils.h"
#include "lib/midr.h"
#include "linux/Tracepoints.h"
#include "linux/perf/IPerfGroups.h"
#include "linux/perf/PerfDriverConfiguration.h"
#include "linux/perf/PerfEventGroupIdentifier.h"
#include "linux/perf/attr_to_key_mapping_tracker.h"
#include "linux/perf/metric_key_to_event_key_tracker.h"
#include "metrics/definitions.hpp"
#include "metrics/group_generator.hpp"
#include "metrics/metric_group_set.hpp"
#include "xml/PmuXML.h"

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <mxml.h>
#include <unistd.h>

static constexpr std::uint32_t TYPE_DERIVED = ~0U;
static constexpr std::uint32_t TYPE_METRIC = ~1U;

// NOLINTBEGIN(modernize-macro-to-enum) - from driver/perf/arm_spe_pmu.c
// TODO: we could read them from /sys/devices/arm_spe_0/format/*
#define SPE_ts_enable_CFG config /* PMSCR_EL1.TS */
#define SPE_ts_enable_LO 0
#define SPE_ts_enable_HI 0
#define SPE_pa_enable_CFG config /* PMSCR_EL1.PA */
#define SPE_pa_enable_LO 1
#define SPE_pa_enable_HI 1
#define SPE_pct_enable_CFG config /* PMSCR_EL1.PCT */
#define SPE_pct_enable_LO 2
#define SPE_pct_enable_HI 2

#define SPE_jitter_CFG config /* PMSIRR_EL1.RND */
#define SPE_jitter_LO 16
#define SPE_jitter_HI 16

#define SPE_branch_filter_CFG config /* PMSFCR_EL1.B */
#define SPE_branch_filter_LO 32
#define SPE_branch_filter_HI 32
#define SPE_load_filter_CFG config /* PMSFCR_EL1.LD */
#define SPE_load_filter_LO 33
#define SPE_load_filter_HI 33
#define SPE_store_filter_CFG config /* PMSFCR_EL1.ST */
#define SPE_store_filter_LO 34
#define SPE_store_filter_HI 34

#define SPE_event_filter_CFG config1 /* PMSEVFR_EL1 */
#define SPE_event_filter_LO 0
#define SPE_event_filter_HI 63

#define SPE_min_latency_CFG config2 /* PMSLATFR_EL1.MINLAT */
#define SPE_min_latency_LO 0
#define SPE_min_latency_HI 11

#define SPE_inv_event_filter_CFG config3 /* PMSNEVFR_EL1 */
#define SPE_inv_event_filter_LO 0
#define SPE_inv_event_filter_HI 63

// NOLINTEND(modernize-macro-to-enum)

// An improved version would mask out old value be we assume 0
#define SET_SPE_CFG(cfg, value) SPE_##cfg##_CFG |= static_cast<uint64_t>(value) << SPE_##cfg##_LO

static constexpr uint64_t armv7AndLaterClockCyclesEvent = 0x11;
static constexpr uint64_t armv7PmuDriverCycleCounterPseudoEvent = 0xFF;
static constexpr uint64_t SPE_DEFAULT_SAMPLE_RATE = 100000; // approx 10KHz sample rate at 1GHz CPU clock

namespace {
    std::string metric_counter_name(PerfCpu const & pmu,
                                    metrics::metric_cpu_version_t const & version,
                                    metrics::metric_events_set_t const & metrics_set)
    {
        // uses the counter set, not the id, since the metrics are shared by all derivitives
        if (version.is_common()) {
            return lib::dyn_printf_str_t("%s_metric_%s", pmu.gator_cpu.getCounterSet(), metrics_set.identifier.data());
        }

        return lib::dyn_printf_str_t("%s_metric_%s_%u_%u",
                                     pmu.gator_cpu.getCounterSet(),
                                     metrics_set.identifier.data(),
                                     version.major_version,
                                     version.minor_version);
    }

    // NOLINTNEXTLINE(misc-no-recursion)
    void flatten_hierarchy_into(std::vector<std::reference_wrapper<metrics::metric_events_set_t const>> & result,
                                lib::Span<combined_metrics_hierarchy_entry_t const> events)
    {
        for (auto const & event : events) {
            result.emplace_back(event.metric);

            flatten_hierarchy_into(result, event.children);
        }
    }

    [[nodiscard]] std::vector<std::reference_wrapper<metrics::metric_events_set_t const>> flatten_hierarchy(
        lib::Span<combined_metrics_hierarchy_entry_t const> events)
    {
        std::vector<std::reference_wrapper<metrics::metric_events_set_t const>> result {};

        flatten_hierarchy_into(result, events);

        return result;
    }

    [[nodiscard]] std::unordered_map<metrics::metric_events_set_t const *, int> make_metric_to_key_map(
        PerfCpu const & pmu,
        std::unordered_map<std::string, int> const & metric_counter_keys,
        metrics::metric_cpu_version_t const & cpu_version,
        lib::Span<std::reference_wrapper<metrics::metric_events_set_t const> const> events)
    {
        std::unordered_map<metrics::metric_events_set_t const *, int> result {};

        for (metrics::metric_events_set_t const & metrics_set : events) {
            auto const counter_name = metric_counter_name(pmu, cpu_version, metrics_set);
            if (auto const it = metric_counter_keys.find(counter_name); it != metric_counter_keys.end()) {
                result.try_emplace(&metrics_set, it->second);
            }
        }

        return result;
    }

    std::function<bool(metrics::metric_events_set_t const &)> make_metric_filter(
        PerfCpu const & pmu,
        std::unordered_map<std::string, int> const & metric_counter_keys,
        metrics::metric_cpu_version_t const & cpu_version,
        lib::Span<std::reference_wrapper<metrics::metric_events_set_t const> const> events)
    {
        std::unordered_set<metrics::metric_events_set_t const *> valid_sets {};

        for (metrics::metric_events_set_t const & metrics_set : events) {
            auto const counter_name = metric_counter_name(pmu, cpu_version, metrics_set);
            if (metric_counter_keys.count(counter_name) != 0) {
                valid_sets.insert(&metrics_set);
            }
        }

        return [valid_sets = std::move(valid_sets)](metrics::metric_events_set_t const & set) {
            return valid_sets.count(&set) != 0;
        };
    }

    [[nodiscard]] bool add_one_metric_event(IPerfGroups & group,
                                            attr_to_key_mapping_tracker_t & mapping_tracker,
                                            PerfCpu const & cluster,
                                            std::size_t group_ndx,
                                            std::uint16_t event_code,
                                            std::uint64_t rate,
                                            std::uint32_t window,
                                            std::unordered_map<std::uint16_t, int> & event_to_key,
                                            bool ebs,
                                            bool pinnable)
    {

        LOG_DEBUG("Metric [%zu] = 0x%04x, rate=%llu", group_ndx, event_code, static_cast<unsigned long long>(rate));
        const int key = group.nextDummyKey();
        IPerfGroups::Attr attr {};

        attr.type = std::uint32_t(cluster.pmu_type);
        attr.config = event_code;
        attr.periodOrFreq = (rate - window);
        attr.strobePeriod = window;
        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        attr.sampleType = (PERF_SAMPLE_TID | PERF_SAMPLE_READ | PERF_SAMPLE_IP | PERF_SAMPLE_CALLCHAIN);
        attr.userspace_only = !ebs;
        attr.metric = true;
        attr.ebs = ebs;
        attr.pinnable = pinnable;

        if (!group.add(mapping_tracker, PerfEventGroupIdentifier(cluster.gator_cpu, group_ndx), key, attr)) {
            LOG_DEBUG("Failed to add metrics group counter");
            return false;
        }

        event_to_key[event_code] = key;
        return true;
    }

    void track_metric_events(metric_key_to_event_key_tracker_t & metric_tracker,
                             std::unordered_map<const metrics::metric_events_set_t *, int> const & set_to_key,
                             std::unordered_map<std::uint16_t, int> const & event_to_key,
                             std::uint16_t const cpu_cycles_event,
                             metrics::metric_events_set_t const & set,
                             std::uint16_t branch_return_event)
    {
        auto const set_key = set_to_key.at(&set);

        // cycle count
        {
            auto const event_key = event_to_key.at(cpu_cycles_event);
            LOG_DEBUG("Metric %s:%u maps key %i to 0x%04x:%i",
                      set.identifier.data(),
                      set.instance_no,
                      set_key,
                      cpu_cycles_event,
                      event_key);
            metric_tracker(set_key,
                           cpu_cycles_event,
                           event_key,
                           metric_key_to_event_key_tracker_t::metric_event_type_t::cycle_counter);
        }

        // branch return
        if (branch_return_event != 0) {
            auto const event_key = event_to_key.at(branch_return_event);
            LOG_DEBUG("Metric %s:%u maps key %i to 0x%04x:%i",
                      set.identifier.data(),
                      set.instance_no,
                      set_key,
                      branch_return_event,
                      event_key);
            metric_tracker(set_key,
                           branch_return_event,
                           event_key,
                           metric_key_to_event_key_tracker_t::metric_event_type_t::return_counter);
        }

        // other events
        for (auto const & event : set.event_codes) {
            if ((event.code == cpu_cycles_event) || (event.code == branch_return_event)) {
                continue;
            }
            auto it = event_to_key.find(event.code);
            runtime_assert(it != event_to_key.end(), "Missing event_to_key for metric event");
            auto const event_key = it->second;
            LOG_DEBUG("Metric %s:%u maps key %i to 0x%04x:%i",
                      set.identifier.data(),
                      set.instance_no,
                      set_key,
                      event.code,
                      event_key);
            metric_tracker(set_key,
                           event.code,
                           event_key,
                           metric_key_to_event_key_tracker_t::metric_event_type_t::event);
        }
    }

    [[nodiscard]] std::size_t get_n_used_pmu_counters(
        std::map<PerfEventGroupIdentifier, std::size_t> const & cpu_event_counts,
        PerfCpu const & cluster)
    {
        if (auto const it = cpu_event_counts.find(PerfEventGroupIdentifier(cluster.gator_cpu));
            it != cpu_event_counts.end()) {
            return it->second;
        }

        return 0;
    }

    static metrics::metric_cpu_events_t const empty_common_metrics_events {};
    static metrics::metric_cpu_version_map_entry_t const empty_common_metrics {
        empty_common_metrics_events,
        0,
    };

    [[nodiscard]] metrics::metric_cpu_version_map_entry_t const & get_common_metrics_version(
        metrics::metric_cpu_event_map_entry_t const & cpu_metrics)
    {
        if (auto it = cpu_metrics.per_version_metrics.find(metrics::metric_cpu_version_t());
            it != cpu_metrics.per_version_metrics.end()) {
            return it->second;
        }

        return empty_common_metrics;
    }

    [[nodiscard]] std::pair<metrics::metric_cpu_version_t, metrics::metric_cpu_version_map_entry_t const *>
    get_specific_metrics_version(metrics::metric_cpu_event_map_entry_t const & cpu_metrics,
                                 GatorCpu const & cpu,
                                 std::unordered_map<cpu_utils::cpuid_t, metrics::metric_cpu_version_t> const & versions)
    {
        // find the version
        metrics::metric_cpu_version_t version;
        for (auto const & cpuid : cpu.getCpuIds()) {
            if (auto const it = versions.find(cpuid); it != versions.end()) {
                version = (version.is_common() ? it->second : std::min(it->second, version));
            }
        }

        if (version.is_common()) {
            return {{}, nullptr};
        }

        // walk through the list to find the highest version that is <= to the requested version
        metrics::metric_cpu_version_t result_version;
        metrics::metric_cpu_version_map_entry_t const * result = nullptr;
        for (auto const & [mvers, metrics] : cpu_metrics.per_version_metrics) {
            if (mvers.is_common()) {
                continue;
            }

            if (version < mvers) {
                break;
            }

            if ((!result_version.is_common()) && (mvers < result_version)) {
                continue;
            }

            result_version = mvers;
            result = &metrics;
        }

        if (result != nullptr) {
            LOG_DEBUG("Matching MIDR version %u.%u to metrics version %u.%u",
                      version.major_version,
                      version.minor_version,
                      result_version.major_version,
                      result_version.minor_version);
        }

        return {result_version, result};
    }

    [[nodiscard]] bool add_metrics_for_strobed(
        IPerfGroups & group,
        attr_to_key_mapping_tracker_t & mapping_tracker,
        metric_key_to_event_key_tracker_t & metric_tracker,
        std::uint16_t const cpu_cycles_event,
        PerfCpu const & cluster,
        std::uint16_t const return_event_code,
        std::unordered_map<metrics::metric_events_set_t const *, int> const & set_to_key,
        std::vector<metrics::combination_t> const & combinations,
        unsigned long const rate,
        unsigned long const window,
        bool const use_return_counter)
    {
        runtime_assert(window > 0 && rate > 0, "Strobed mode requires rate/window > 0");

        // output each of the combinations as a seperate multiplexed group
        for (std::size_t n = 0; n < combinations.size(); ++n) {
            std::uint32_t const group_ndx = n + 1;
            std::unordered_map<std::uint16_t, int> event_to_key {};
            bool contains_return_event = false;

            // add the leader
            group.addGroupLeader(mapping_tracker, PerfEventGroupIdentifier {cluster.gator_cpu, group_ndx}, false);

            // add cycles, which is the sampling event
            if (!add_one_metric_event(group,
                                      mapping_tracker,
                                      cluster,
                                      group_ndx,
                                      cpu_cycles_event,
                                      rate,
                                      window,
                                      event_to_key,
                                      false,
                                      false)) {
                return false;
            }

            // add the metric events (which are not sampling)
            for (auto const event_code : combinations[n].event_codes) {
                // no need to add it twice
                if (event_code == cpu_cycles_event) {
                    continue;
                }

                if (!add_one_metric_event(group,
                                          mapping_tracker,
                                          cluster,
                                          group_ndx,
                                          event_code,
                                          0,
                                          0,
                                          event_to_key,
                                          false,
                                          false)) {
                    return false;
                }

                contains_return_event |= (event_code == return_event_code);
            }

            // add branch-return counter for checking
            if (use_return_counter && !contains_return_event
                && !add_one_metric_event(group,
                                         mapping_tracker,
                                         cluster,
                                         group_ndx,
                                         return_event_code,
                                         0,
                                         0,
                                         event_to_key,
                                         false,
                                         false)) {
                return false;
            }

            // add the mappings
            for (auto const * set : combinations[n].contains_sets) {
                track_metric_events(metric_tracker,
                                    set_to_key,
                                    event_to_key,
                                    cpu_cycles_event,
                                    *set,
                                    use_return_counter ? return_event_code : 0);
            }
        }

        return true;
    }

    [[nodiscard]] bool add_metrics_for_ebs(
        IPerfGroups & group,
        attr_to_key_mapping_tracker_t & mapping_tracker,
        metric_key_to_event_key_tracker_t & metric_tracker,
        std::uint16_t const cpu_cycles_event,
        PerfCpu const & cluster,
        std::unordered_map<metrics::metric_events_set_t const *, int> const & set_to_key,
        std::vector<metrics::combination_t> const & combinations,
        unsigned long const rate)
    {
        runtime_assert(rate > 0, "EBS mode requires non-zero sample period");

        auto const pinnable = (combinations.size() == 1);

        // output each of the combinations as a seperate multiplexed group
        for (std::size_t n = 0; n < combinations.size(); ++n) {
            std::uint32_t const group_ndx = n + 1;
            std::unordered_map<std::uint16_t, int> event_to_key {};

            auto const ebs_ratio = combinations[n].ebs_ratio;
            auto const uses_cycles = combinations[n].uses_cycles;
            auto const sample_period = std::max<unsigned long>(1, rate / (1U * (ebs_ratio > 0 ? ebs_ratio
                                                                                              : 1)));
            auto const cycles_period = (uses_cycles ? sample_period : rate);

            LOG_DEBUG("Metric group #%zu has ebs_ratio=%llu, uses_cycles=%c, cycles_period=%lu, sample_period=%lu",
                      n + 1,
                      static_cast<unsigned long long>(ebs_ratio),
                      uses_cycles ? 'Y' : 'N',
                      cycles_period,
                      sample_period);

            runtime_assert((ebs_ratio == 1) || !uses_cycles, "Unexpected ebs_ratio value with uses_cycles");

            // add the leader
            group.addGroupLeader(mapping_tracker, PerfEventGroupIdentifier {cluster.gator_cpu, group_ndx}, true);

            // add cycles
            if (!add_one_metric_event(group,
                                      mapping_tracker,
                                      cluster,
                                      group_ndx,
                                      cpu_cycles_event,
                                      cycles_period,
                                      0,
                                      event_to_key,
                                      true,
                                      pinnable)) {
                return false;
            }

            // add the metric events (EBS sampling, in addition to cycles)
            for (auto const event_code : combinations[n].event_codes) {
                // no need to add it twice
                if (event_code == cpu_cycles_event) {
                    continue;
                }

                if (!add_one_metric_event(group,
                                          mapping_tracker,
                                          cluster,
                                          group_ndx,
                                          event_code,
                                          sample_period,
                                          0,
                                          event_to_key,
                                          true,
                                          false)) {
                    return false;
                }
            }

            // add the mappings
            for (auto const * set : combinations[n].contains_sets) {
                track_metric_events(metric_tracker, set_to_key, event_to_key, cpu_cycles_event, *set, 0);
            }
        }

        return true;
    }

    [[nodiscard]] bool add_metrics_for(IPerfGroups & group,
                                       attr_to_key_mapping_tracker_t & mapping_tracker,
                                       metric_key_to_event_key_tracker_t & metric_tracker,
                                       std::map<PerfEventGroupIdentifier, std::size_t> const & cpu_event_counts,
                                       std::unordered_map<std::string, int> const & metric_ids,
                                       std::uint16_t const cpu_cycles_event,
                                       bool strobing_mode,
                                       PerfCpu const & cluster,
                                       std::uint16_t return_event_code,
                                       combined_metrics_t const & combined_metrics)
    {
        auto const n_used = get_n_used_pmu_counters(cpu_event_counts, cluster);
        auto const n_available_raw = cluster.gator_cpu.getPmncCounters() //
                                   - std::min<std::size_t>(n_used, cluster.gator_cpu.getPmncCounters());

        // counting return events is only enabled if there is space. Prioritize collecting metrics when there is limited number of programmable counters available
        auto const use_return_counter = (return_event_code != 0) //
                                     && strobing_mode            //
                                     && ((combined_metrics.largest_metric_event_count + 1) <= n_available_raw);

        // counting return events consumes one counter
        auto const n_available = (use_return_counter ? std::max<std::size_t>(1, n_available_raw) - 1 //
                                                     : n_available_raw);

        LOG_FINE("Found metric set for core type %s, n_counters=%i (used %zu, raw %zu, ret %u, avail %zu, size %zu), "
                 "strobing_mode=%c",
                 cluster.gator_cpu.getCoreName(),
                 cluster.gator_cpu.getPmncCounters(),
                 n_used,
                 n_available_raw,
                 use_return_counter,
                 n_available,
                 combined_metrics.total_num_events,
                 strobing_mode ? 'y' : 'n');

        // flatten out the hierarchy tree
        auto const flattened_events = flatten_hierarchy(combined_metrics.root_events);

        // make a lookup from metric set to counter key .
        // this is used by streamline to correlate the perf ids via there keys back to the original event code / metric(s)
        auto const set_to_key = make_metric_to_key_map(cluster, metric_ids, combined_metrics.version, flattened_events);

        // find the valid metric combinations. this is the smallest set of multiplexed counter groups that will fit all valid metrics
        auto const combinations = metrics::make_combinations(
            !strobing_mode,
            n_available,
            flattened_events,
            make_metric_filter(cluster, metric_ids, combined_metrics.version, flattened_events));

        LOG_DEBUG("Combinations set size %zu", combinations.size());

        // select the sample rate and strobe window
        constexpr unsigned one_billion = 1'000'000'000U;
        auto const sample_rate = std::min(gSessionData.mSampleRate > 0 ? unsigned(gSessionData.mSampleRate) //
                                                                       : 1000U,
                                          one_billion);
        auto const long_period = one_billion / sample_rate;

        if (strobing_mode) {
            constexpr unsigned short_period = 100U;

            return add_metrics_for_strobed(group,
                                           mapping_tracker,
                                           metric_tracker,
                                           cpu_cycles_event,
                                           cluster,
                                           return_event_code,
                                           set_to_key,
                                           combinations,
                                           long_period,
                                           short_period,
                                           use_return_counter);
        }

        return add_metrics_for_ebs(group,
                                   mapping_tracker,
                                   metric_tracker,
                                   cpu_cycles_event,
                                   cluster,
                                   set_to_key,
                                   combinations,
                                   long_period);
    }

    bool enableGatorTracePoint(IPerfGroups & group, attr_to_key_mapping_tracker_t & mapping_tracker, long long id)
    {
        IPerfGroups::Attr attr;
        attr.type = PERF_TYPE_TRACEPOINT;
        attr.config = id;
        attr.periodOrFreq = 1;
        attr.sampleType = PERF_SAMPLE_RAW;
        const auto key = getEventKey();
        return group.add(mapping_tracker, PerfEventGroupIdentifier(), key, attr, false);
    }

    [[nodiscard]] std::unordered_map<cpu_utils::cpuid_t, metrics::metric_cpu_version_t> map_cpu_metric_versions(
        const ICpuInfo & cpuInfo)
    {
        std::unordered_map<cpu_utils::cpuid_t, metrics::metric_cpu_version_t> result {};

        for (auto const & midr : cpuInfo.getMidrs()) {
            metrics::metric_cpu_version_t const version {midr.get_variant(), midr.get_revision()};

            auto [it, inserted] = result.try_emplace(midr.to_cpuid(), version);

            if ((!inserted) && (it->second != version)) {
                // Unexpected mismatch
                LOG_DEBUG("CPUID 0x%05x maps to two different product versions: %u.%u and %u.%u",
                          midr.to_cpuid().to_raw_value(),
                          it->second.major_version,
                          it->second.minor_version,
                          version.major_version,
                          version.minor_version);
                // just pick the minimum
                it->second = std::min(it->second, version);
            }
        }

        return result;
    }

    // NOLINTNEXTLINE(misc-no-recursion)
    [[nodiscard]] std::size_t combine_metrics_recursive(
        std::unordered_set<std::string_view> & seen_ids,
        std::initializer_list<metrics::metric_hierarchy_entry_t> const & events,
        std::vector<combined_metrics_hierarchy_entry_t> & result)
    {
        std::size_t total_num_events = 0;

        for (auto const & entry : events) {
            auto const & metric = entry.metric.get();
            if (auto const [it, inserted] = seen_ids.insert(metric.identifier); inserted) {
                (void) it; //GCC7 :-(

                auto & child = result.emplace_back(entry);

                total_num_events += 1;
                total_num_events += combine_metrics_recursive(seen_ids, entry.children, child.children);
            }
        }

        return total_num_events;
    }

    [[nodiscard]] combined_metrics_t combine_metrics(
        metrics::metric_cpu_version_map_entry_t const & cpu_metrics_common,
        metrics::metric_cpu_version_t const & version,
        metrics::metric_cpu_version_map_entry_t const * cpu_metrics_version)
    {
        std::unordered_set<std::string_view> seen_ids {};

        combined_metrics_t result {version};

        if (cpu_metrics_version != nullptr) {
            result.largest_metric_event_count = std::max(cpu_metrics_version->largest_metric_event_count,
                                                         cpu_metrics_common.largest_metric_event_count);

            result.total_num_events =
                combine_metrics_recursive(seen_ids, cpu_metrics_version->root_events.get(), result.root_events);
        }
        else {
            result.largest_metric_event_count = cpu_metrics_common.largest_metric_event_count;
        }

        result.total_num_events +=
            combine_metrics_recursive(seen_ids, cpu_metrics_common.root_events.get(), result.root_events);

        return result;
    }
}

class PerfCounter : public DriverCounter {
public:
    static constexpr uint64_t noConfigId2 = ~0ULL;
    static constexpr bool fixUpClockCyclesEventDefault = false;

    PerfCounter(DriverCounter * next,
                const PerfEventGroupIdentifier & groupIdentifier,
                const char * name,
                const IPerfGroups::Attr & attr,
                bool usesAux,
                uint64_t config_id2 = noConfigId2,
                bool fixUpClockCyclesEvent = fixUpClockCyclesEventDefault)
        : DriverCounter(next, name),
          eventGroupIdentifier(groupIdentifier),
          attr(attr),
          mConfigId2(config_id2),
          mFixUpClockCyclesEvent(fixUpClockCyclesEvent),
          mUsesAux(usesAux)
    {
    }

    PerfCounter(DriverCounter * next,
                const PerfEventGroupIdentifier & groupIdentifier,
                const char * name,
                uint32_t type,
                uint64_t config,
                uint64_t sampleType,
                uint64_t count,
                uint64_t config_id2 = noConfigId2,
                bool fixUpClockCyclesEvent = fixUpClockCyclesEventDefault,
                std::unordered_set<metrics::metric_group_id_t> metricGroups = {})
        : PerfCounter(next, groupIdentifier, name, IPerfGroups::Attr {}, false, config_id2, fixUpClockCyclesEvent)
    {
        attr.type = type;
        attr.config = config;
        attr.periodOrFreq = count;
        attr.sampleType = sampleType;
        attr.ebs = true;
        mMetricGroups = std::move(metricGroups);
    }

    // Intentionally undefined
    PerfCounter(const PerfCounter &) = delete;
    PerfCounter & operator=(const PerfCounter &) = delete;
    PerfCounter(PerfCounter &&) = delete;
    PerfCounter & operator=(PerfCounter &&) = delete;

    using DriverCounter::read;

    [[nodiscard]] virtual bool isCpuFreqCounterFor(GatorCpu const & /*cluster*/) const { return false; }

    virtual void read(IPerfAttrsConsumer & /*unused*/, const int /* cpu */, const GatorCpu * /* cluster */) {}

    [[nodiscard]] inline const PerfEventGroupIdentifier & getPerfEventGroupIdentifier() const
    {
        return eventGroupIdentifier;
    }

    [[nodiscard]] IPerfGroups::Attr getAttr() const { return attr; }

    [[nodiscard]] inline bool hasConfigId2() const { return mConfigId2 != noConfigId2; }

    [[nodiscard]] inline bool usesAux() const { return mUsesAux; }

    [[nodiscard]] IPerfGroups::Attr getAttr2() const
    {
        IPerfGroups::Attr attr2 {attr};
        attr2.config = mConfigId2;
        return attr2;
    }

    inline void setCount(const uint64_t count) { attr.periodOrFreq = count; }

    inline void setConfig(const uint64_t config)
    {
        // The Armv7 PMU driver in the linux kernel uses a special event number for the cycle counter
        // that is different from the clock cycles event number.
        // https://github.com/torvalds/linux/blob/0adb32858b0bddf4ada5f364a84ed60b196dbcda/arch/arm/kernel/perf_event_v7.c#L1042
        if (mFixUpClockCyclesEvent && config == armv7AndLaterClockCyclesEvent) {
            attr.config = armv7PmuDriverCycleCounterPseudoEvent;
        }
        else {
            attr.config = config;
        }
    }

    inline void setConfig1(const uint64_t config) { attr.config1 = config; }

    inline void setConfig2(const uint64_t config) { attr.config2 = config; }

    inline void setConfig3(const uint64_t config) { attr.config3 = config; }

    inline void setSampleType(uint64_t sampleType) { attr.sampleType = sampleType; }

    [[nodiscard]] bool supportsAtLeastOne(metrics::metric_group_set_t const & desired) const override
    {
        return std::any_of(mMetricGroups.cbegin(), mMetricGroups.cend(), [&](auto const & metric_group_id) {
            return desired.has_member(metric_group_id);
        });
    }

private:
    const PerfEventGroupIdentifier eventGroupIdentifier;
    IPerfGroups::Attr attr;
    const uint64_t mConfigId2;
    bool mFixUpClockCyclesEvent;
    bool mUsesAux;
    // Where this PerfCounter represents a metric, this member represents
    // the groups it is a part of.
    std::unordered_set<metrics::metric_group_id_t> mMetricGroups {};
};

class CPUFreqDriver : public PerfCounter {
public:
    CPUFreqDriver(DriverCounter * next, const char * name, uint64_t id, const GatorCpu & cluster, bool use_cpuinfo)
        : PerfCounter(next, PerfEventGroupIdentifier(cluster), name, PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_RAW, 1),
          use_cpuinfo(use_cpuinfo)
    {
    }

    // Intentionally undefined
    CPUFreqDriver(const CPUFreqDriver &) = delete;
    CPUFreqDriver & operator=(const CPUFreqDriver &) = delete;
    CPUFreqDriver(CPUFreqDriver &&) = delete;
    CPUFreqDriver & operator=(CPUFreqDriver &&) = delete;

    [[nodiscard]] bool isCpuFreqCounterFor(GatorCpu const & cluster) const override
    {
        return cluster == *getPerfEventGroupIdentifier().getCluster();
    }

    [[nodiscard]] bool isUseCpuInfoPath() const { return use_cpuinfo; }

    void read(IPerfAttrsConsumer & attrsConsumer, const int cpu, const GatorCpu * cluster) override
    {
        constexpr std::size_t buffer_size = 128;
        constexpr std::int64_t freq_multiplier = 1000;

        if (cluster == nullptr || !(*cluster == *getPerfEventGroupIdentifier().getCluster())) {
            return;
        }

        char const * const pattern = (use_cpuinfo ? "/sys/devices/system/cpu/cpu%i/cpufreq/cpuinfo_cur_freq"
                                                  : "/sys/devices/system/cpu/cpu%i/cpufreq/scaling_cur_freq");

        lib::printf_str_t<buffer_size> buffer {pattern, cpu};
        int64_t freq;
        if (lib::readInt64FromFile(buffer, freq) != 0) {
            freq = 0;
        }
        attrsConsumer.perfCounter(cpu, getKey(), freq_multiplier * freq);
    }

private:
    // use the cpuinfo_xxx files rather than the scaling_xxx files
    bool use_cpuinfo;
};

template<typename T>
inline static T & neverNull(T * t)
{
    if (t == nullptr) {
        handleException();
    }
    return *t;
}

static long long getTracepointId_(const TraceFsConstants & traceFsConstants, const char * counter, const char * name)
{
    long long result = getTracepointId(traceFsConstants, name);
    if (result <= 0) {
        LOG_SETUP("%s is disabled\n%s was not found", counter, getTracepointPath(traceFsConstants, name, "id").c_str());
    }
    LOG_DEBUG("Tracepoint %s ID is %lld", name, result);
    return result;
}

static std::int64_t getTracepointId_(const TraceFsConstants & traceFsConstants, const char * name)
{
    auto result = getTracepointId(traceFsConstants, name);
    if (result <= 0) {
        LOG_SETUP("%s is disabled\n%s was not found", name, getTracepointPath(traceFsConstants, name, "id").c_str());
    }
    LOG_DEBUG("Tracepoint %s ID is %" PRId64, name, result);
    return result;
}

PerfDriver::PerfDriver(PerfDriverConfiguration && configuration,
                       PmuXML && pmuXml,
                       const char * maliFamilyName,
                       const ICpuInfo & cpuInfo,
                       const TraceFsConstants & traceFsConstants,
                       bool disableKernelAnnotations)
    : SimpleDriver("Perf"),
      traceFsConstants(traceFsConstants),
      mTracepoints(nullptr),
      mConfig(std::move(configuration)),
      mPmuXml(pmuXml),
      mCpuInfo(cpuInfo),
      cpu_metric_versions(map_cpu_metric_versions(cpuInfo)),
      mDisableKernelAnnotations(disableKernelAnnotations)
{
    static constexpr std::size_t buffer_size = 64;

    // add CPU PMUs
    for (const auto & perfCpu : mConfig.cpus) {

        if ((perfCpu.pmu_type != PERF_TYPE_RAW) && (perfCpu.pmu_type != PERF_TYPE_HARDWARE)) {
            LOG_DEBUG("Adding cpu counters for %s with type %i", perfCpu.gator_cpu.getCoreName(), perfCpu.pmu_type);
        }
        else if ((perfCpu.gator_cpu.getCpuIds().size() > 1)
                 || (!perfCpu.gator_cpu.hasCpuId(cpu_utils::cpuid_t::other))) {
            LOG_DEBUG("Adding cpu counters (based on cpuid) for %s", perfCpu.gator_cpu.getCoreName());
        }
        else {
            LOG_DEBUG("Adding cpu counters based on default CPU object");
        }

        addCpuCounters(perfCpu);
    }

    // add uncore PMUs
    for (const auto & perfUncore : mConfig.uncores) {
        LOG_DEBUG("Adding uncore counters for %s %s with type %i",
                  perfUncore.uncore_pmu.getCoreName(),
                  perfUncore.uncore_pmu.getDeviceInstance() != nullptr ? perfUncore.uncore_pmu.getDeviceInstance() //
                                                                       : "",
                  perfUncore.pmu_type);
        addUncoreCounters(perfUncore);
    }

    // Add supported software counters
    long long id;

    if (getConfig().can_access_tracepoints) {
        id = getTracepointId_(traceFsConstants, "Interrupts: SoftIRQ", "irq/softirq_exit");
        if (id >= 0) {
            for (const auto & perfCpu : mConfig.cpus) {
                lib::printf_str_t<buffer_size> buf {"%s_softirq", perfCpu.gator_cpu.getId()};
                setCounters(new PerfCounter(getCounters(),
                                            PerfEventGroupIdentifier(perfCpu.gator_cpu),
                                            buf,
                                            PERF_TYPE_TRACEPOINT,
                                            id,
                                            PERF_SAMPLE_READ,
                                            0));
            }
        }

        id = getTracepointId_(traceFsConstants, "Interrupts: IRQ", "irq/irq_handler_exit");
        if (id >= 0) {
            for (const auto & perfCpu : mConfig.cpus) {
                lib::printf_str_t<buffer_size> buf {"%s_irq", perfCpu.gator_cpu.getId()};
                setCounters(new PerfCounter(getCounters(),
                                            PerfEventGroupIdentifier(perfCpu.gator_cpu),
                                            buf,
                                            PERF_TYPE_TRACEPOINT,
                                            id,
                                            PERF_SAMPLE_READ,
                                            0));
            }
        }

        id = getTracepointId_(traceFsConstants, "Scheduler: Switch", SCHED_SWITCH);
        if (id >= 0) {
            for (const auto & perfCpu : mConfig.cpus) {
                lib::printf_str_t<buffer_size> buf {"%s_switch", perfCpu.gator_cpu.getId()};
                setCounters(new PerfCounter(getCounters(),
                                            PerfEventGroupIdentifier(perfCpu.gator_cpu),
                                            buf,
                                            PERF_TYPE_TRACEPOINT,
                                            id,
                                            PERF_SAMPLE_READ,
                                            0));
            }
        }

        if (!mConfig.config.use_ftrace_for_cpu_frequency) {
            id = getTracepointId_(traceFsConstants, "Clock: Frequency", CPU_FREQUENCY);
            bool const has_cpuinfo = (access("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq", R_OK) == 0);
            bool const has_scaling = (access("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", R_OK) == 0);
            if ((id >= 0) && (has_cpuinfo || has_scaling)) {
                for (const auto & perfCpu : mConfig.cpus) {
                    lib::printf_str_t<buffer_size> buf {"%s_freq", perfCpu.gator_cpu.getId()};
                    setCounters(new CPUFreqDriver(getCounters(), buf, id, perfCpu.gator_cpu, has_cpuinfo));
                }
            }
        }
    }

    if (getConfig().can_access_tracepoints || getConfig().has_attr_context_switch) {
        // can get contention from switch record and tracepoint
        setCounters(new PerfCounter(getCounters(),
                                    PerfEventGroupIdentifier(),
                                    "Linux_cpu_wait_contention",
                                    TYPE_DERIVED,
                                    -1,
                                    0,
                                    0));
        // iowait only from tracepoint
        if (getConfig().can_access_tracepoints) {
            setCounters(new PerfCounter(getCounters(),
                                        PerfEventGroupIdentifier(),
                                        "Linux_cpu_wait_io",
                                        TYPE_DERIVED,
                                        -1,
                                        0,
                                        0));
        }

        // add kernel/user time
        for (const auto & perfCpu : mConfig.cpus) {
            if (!getConfig().exclude_kernel) {
                lib::printf_str_t<buffer_size> const buf {"%s_system", perfCpu.gator_cpu.getId()};
                setCounters(new PerfCounter(getCounters(),
                                            PerfEventGroupIdentifier(perfCpu.gator_cpu),
                                            buf,
                                            TYPE_DERIVED,
                                            -1,
                                            0,
                                            0));
            }

            lib::printf_str_t<buffer_size> const buf {"%s_user", perfCpu.gator_cpu.getId()};
            setCounters(new PerfCounter(getCounters(),
                                        PerfEventGroupIdentifier(perfCpu.gator_cpu),
                                        buf,
                                        TYPE_DERIVED,
                                        -1,
                                        0,
                                        0));
        }
    }

    // add
    if (maliFamilyName != nullptr) {
        // add midgard software tracepoints
        addMidgardHwTracepoints(maliFamilyName);
    }

    //Adding for performance counters for perf software
    setCounters(new PerfCounter(getCounters(),
                                PerfEventGroupIdentifier(),
                                "PERF_COUNT_SW_CPU_CLOCK",
                                PERF_TYPE_SOFTWARE,
                                PERF_COUNT_SW_CPU_CLOCK,
                                0,
                                0));
    setCounters(new PerfCounter(getCounters(),
                                PerfEventGroupIdentifier(),
                                "PERF_COUNT_SW_TASK_CLOCK",
                                PERF_TYPE_SOFTWARE,
                                PERF_COUNT_SW_TASK_CLOCK,
                                0,
                                0));
    if (!getConfig().exclude_kernel) {
        // requires ability to read kernel events
        setCounters(new PerfCounter(getCounters(),
                                    PerfEventGroupIdentifier(),
                                    "PERF_COUNT_SW_CONTEXT_SWITCHES",
                                    PERF_TYPE_SOFTWARE,
                                    PERF_COUNT_SW_CONTEXT_SWITCHES,
                                    0,
                                    0));
    }
    setCounters(new PerfCounter(getCounters(),
                                PerfEventGroupIdentifier(),
                                "PERF_COUNT_SW_CPU_MIGRATIONS",
                                PERF_TYPE_SOFTWARE,
                                PERF_COUNT_SW_CPU_MIGRATIONS,
                                0,
                                0));
    setCounters(new PerfCounter(getCounters(),
                                PerfEventGroupIdentifier(),
                                "PERF_COUNT_SW_PAGE_FAULTS",
                                PERF_TYPE_SOFTWARE,
                                PERF_COUNT_SW_PAGE_FAULTS,
                                0,
                                0));
    setCounters(new PerfCounter(getCounters(),
                                PerfEventGroupIdentifier(),
                                "PERF_COUNT_SW_PAGE_FAULTS_MAJ",
                                PERF_TYPE_SOFTWARE,
                                PERF_COUNT_SW_PAGE_FAULTS_MAJ,
                                0,
                                0));
    setCounters(new PerfCounter(getCounters(),
                                PerfEventGroupIdentifier(),
                                "PERF_COUNT_SW_PAGE_FAULTS_MIN",
                                PERF_TYPE_SOFTWARE,
                                PERF_COUNT_SW_PAGE_FAULTS_MIN,
                                0,
                                0));
    setCounters(new PerfCounter(getCounters(),
                                PerfEventGroupIdentifier(),
                                "PERF_COUNT_SW_ALIGNMENT_FAULTS",
                                PERF_TYPE_SOFTWARE,
                                PERF_COUNT_SW_ALIGNMENT_FAULTS,
                                0,
                                0));
    setCounters(new PerfCounter(getCounters(),
                                PerfEventGroupIdentifier(),
                                "PERF_COUNT_SW_EMULATION_FAULTS",
                                PERF_TYPE_SOFTWARE,
                                PERF_COUNT_SW_EMULATION_FAULTS,
                                0,
                                0));
}

class PerfTracepoint {
public:
    PerfTracepoint(PerfTracepoint * const next, const DriverCounter * const counter, const char * const tracepoint)
        : mNext(next), mCounter(counter), mTracepoint(tracepoint)
    {
    }

    // Intentionally undefined
    PerfTracepoint(const PerfTracepoint &) = delete;
    PerfTracepoint & operator=(const PerfTracepoint &) = delete;
    PerfTracepoint(PerfTracepoint &&) = delete;
    PerfTracepoint & operator=(PerfTracepoint &&) = delete;

    [[nodiscard]] PerfTracepoint * getNext() const { return mNext; }
    [[nodiscard]] const DriverCounter * getCounter() const { return mCounter; }
    [[nodiscard]] const char * getTracepoint() const { return mTracepoint.c_str(); }

private:
    PerfTracepoint * const mNext;
    const DriverCounter * const mCounter;
    const std::string mTracepoint;
};

PerfDriver::~PerfDriver()
{
    for (PerfTracepoint * nextTracepoint = mTracepoints; nextTracepoint != nullptr;) {
        PerfTracepoint * tracepoint = nextTracepoint;
        nextTracepoint = tracepoint->getNext();
        delete tracepoint;
    }
}

void PerfDriver::addCpuCounters(const PerfCpu & perfCpu)
{
    const GatorCpu & cpu = perfCpu.gator_cpu;
    const int type = perfCpu.pmu_type;

    {
        lib::dyn_printf_str_t name {"%s_ccnt", cpu.getId()};
        setCounters(new PerfCounter(getCounters(),
                                    PerfEventGroupIdentifier(cpu),
                                    name,
                                    type,
                                    -1,
                                    PERF_SAMPLE_READ,
                                    0,
                                    PerfCounter::noConfigId2,
                                    getConfig().has_armv7_pmu_driver));
    }

    for (int j = 0; j < cpu.getPmncCounters(); ++j) {
        lib::dyn_printf_str_t name {"%s_cnt%d", cpu.getId(), j};
        setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(cpu), name, type, -1, PERF_SAMPLE_READ, 0));
    }

    const char * speId = cpu.getSpeName();
    if (speId != nullptr) {
        IPerfGroups::Attr attr;
        attr.sampleType |= PERF_SAMPLE_TID;
        setCounters(new PerfCounter(getCounters(),
                                    PerfEventGroupIdentifier(cpu, mConfig.cpuNumberToSpeType),
                                    speId,
                                    attr,
                                    true));
    }

    auto const * cpu_metrics = metrics::find_events_for_cset(cpu.getCounterSet());
    if (cpu_metrics != nullptr) {
        auto const & cpu_metrics_common = get_common_metrics_version(*cpu_metrics);
        auto const [cpu_version, cpu_metrics_for_version] =
            get_specific_metrics_version(*cpu_metrics, cpu, cpu_metric_versions);
        auto const combined_metrics = combine_metrics(cpu_metrics_common, cpu_version, cpu_metrics_for_version);

        LOG_DEBUG("PMU %s has %zu metrics", cpu.getCoreName(), combined_metrics.total_num_events);

        addCpuCounterMetrics(perfCpu, combined_metrics);
    }
}

void PerfDriver::addCpuCounterMetrics(const PerfCpu & perfCpu, combined_metrics_t const & cpu_metrics)
{
    addCpuCounterMetricsRecursive(perfCpu, cpu_metrics.version, cpu_metrics.root_events);
}

// NOLINTNEXTLINE(misc-no-recursion)
void PerfDriver::addCpuCounterMetricsRecursive(const PerfCpu & perfCpu,
                                               metrics::metric_cpu_version_t const & version,
                                               lib::Span<combined_metrics_hierarchy_entry_t const> events)
{
    const GatorCpu & cpu = perfCpu.gator_cpu;

    for (auto const & entry : events) {
        auto const & metrics_set = entry.metric.get();
        auto const metric_id = metric_counter_name(perfCpu, version, metrics_set);

        LOG_DEBUG("PMU %s has metric %s:%u containing %zu events as %s",
                  cpu.getCoreName(),
                  metrics_set.identifier.data(),
                  metrics_set.instance_no,
                  metrics_set.event_codes.size(),
                  metric_id.c_str());

        std::unordered_set<metrics::metric_group_id_t> groups {metrics_set.groups};
        groups.insert(entry.group);

        if ((cpu.getPmncCounters() > 0) && (metrics_set.event_codes.size() <= std::size_t(cpu.getPmncCounters()))) {
            setCounters(new PerfCounter(getCounters(),
                                        PerfEventGroupIdentifier(cpu, 1),
                                        metric_id.c_str(),
                                        TYPE_METRIC,
                                        -1,
                                        0,
                                        0,
                                        PerfCounter::noConfigId2,
                                        PerfCounter::fixUpClockCyclesEventDefault,
                                        std::move(groups)));
        }

        addCpuCounterMetricsRecursive(perfCpu, version, entry.children);
    }
}

void PerfDriver::addUncoreCounters(const PerfUncore & perfUncore)
{
    const UncorePmu & pmu = perfUncore.uncore_pmu;
    const int type = perfUncore.pmu_type;

    if (pmu.getHasCyclesCounter()) {
        lib::dyn_printf_str_t const name {"%s_ccnt", pmu.getId()};
        setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(pmu), name, type, -1, PERF_SAMPLE_READ, 0));
    }

    for (int j = 0; j < pmu.getPmncCounters(); ++j) {
        lib::dyn_printf_str_t const name {"%s_cnt%d", pmu.getId(), j};
        setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(pmu), name, type, -1, PERF_SAMPLE_READ, 0));
    }
}

void PerfDriver::readEvents(mxml_node_t * const xml)
{
    mxml_node_t * node = xml;

    // Only for use with perf
    if (!getConfig().can_access_tracepoints) {
        return;
    }

    while (true) {
        node = mxmlFindElement(node, xml, "event", nullptr, nullptr, MXML_DESCEND);
        if (node == nullptr) {
            break;
        }
        const char * counter = mxmlElementGetAttr(node, "counter");
        if (counter == nullptr) {
            continue;
        }

        if (strncmp(counter, "ftrace_", 7) != 0) {
            continue;
        }

        const char * tracepoint = mxmlElementGetAttr(node, "tracepoint");
        if (tracepoint == nullptr) {
            const char * regex = mxmlElementGetAttr(node, "regex");
            if (regex == nullptr) {
                LOG_ERROR("The tracepoint counter %s is missing the required tracepoint attribute", counter);
                handleException();
            }
            else {
                LOG_DEBUG("Not using perf for counter %s", counter);
                continue;
            }
        }

        // never process the ftrace cpu frequency counter with perf; perfdriver has its own cpu frequency counters (per cluster)
        const bool is_cpu_frequency = ((tracepoint != nullptr) && (strcmp(tracepoint, "power/cpu_frequency") == 0)
                                       && (strcmp(counter, "ftrace_power_cpu_frequency") == 0));
        if (is_cpu_frequency) {
            LOG_DEBUG("Not using perf for %s", counter);
            continue;
        }

        const char * arg = mxmlElementGetAttr(node, "arg");

        long long id = getTracepointId_(traceFsConstants, counter, tracepoint);
        if (id >= 0) {
            LOG_DEBUG("Using perf for %s", counter);
            setCounters(new PerfCounter(getCounters(),
                                        PerfEventGroupIdentifier(),
                                        counter,
                                        PERF_TYPE_TRACEPOINT,
                                        id,
                                        arg == nullptr ? 0 : PERF_SAMPLE_RAW,
                                        1));
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), tracepoint);
        }
    }
}

#define COUNT_OF(X) (sizeof(X) / sizeof((X)[0]))

void PerfDriver::addMidgardHwTracepoints(const char * const maliFamilyName)
{
    static constexpr std::size_t buffer_size = 256;

    bool isSystemWide = isCaptureOperationModeSystemWide(gSessionData.mCaptureOperationMode);
    bool canAccessTrcPnt = getConfig().can_access_tracepoints;
    if (!isSystemWide || !canAccessTrcPnt) {
        LOG_DEBUG("No Mali Tracepoint counters added, (systemwide (%d), canAccessTracepoints(%d))",
                  isSystemWide,
                  canAccessTrcPnt);
        return;
    }

    static const char * const MALI_MIDGARD_AS_IN_USE_RELEASED[] = {"MMU_AS_0", "MMU_AS_1", "MMU_AS_2", "MMU_AS_3"};

    static const char * const MALI_MIDGARD_PAGE_FAULT_INSERT_PAGES[] = {"MMU_PAGE_FAULT_0",
                                                                        "MMU_PAGE_FAULT_1",
                                                                        "MMU_PAGE_FAULT_2",
                                                                        "MMU_PAGE_FAULT_3"};

    static const char * const MALI_MIDGARD_TOTAL_ALLOC_PAGES = "TOTAL_ALLOC_PAGES";

    long long id;

    // add midgard software tracepoints
    auto addCounterWithConfigId2 = [this](const char * name, uint64_t id, uint64_t configId2) {
        IPerfGroups::Attr attr {};
        attr.type = PERF_TYPE_TRACEPOINT;
        attr.config = id;
        attr.periodOrFreq = 1;
        attr.sampleType = PERF_SAMPLE_RAW;
        attr.task = true;
        attr.ebs = true;
        setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), name, attr, false, configId2, false));
    };
    auto addCounter = [addCounterWithConfigId2](const char * name, uint64_t id) {
        addCounterWithConfigId2(name, id, PerfCounter::noConfigId2);
    };

    id = getTracepointId_(traceFsConstants, MALI_MMU_IN_USE, MALI_TRC_PNT_PATH[MALI_MMU_IN_USE]);
    if (id >= 0) {
        const int id2 = getTracepointId_(traceFsConstants, MALI_PM_STATUS, MALI_TRC_PNT_PATH[MALI_PM_STATUS]);
        for (const auto * i : MALI_MIDGARD_AS_IN_USE_RELEASED) {
            lib::printf_str_t<buffer_size> buf {"ARM_Mali-%s_%s", maliFamilyName, i};
            addCounterWithConfigId2(buf, id, id2);
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), MALI_TRC_PNT_PATH[MALI_MMU_IN_USE]);
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), MALI_TRC_PNT_PATH[MALI_PM_STATUS]);
        }
    }

    id = getTracepointId_(traceFsConstants, MALI_MMU_PAGE_FAULT, MALI_TRC_PNT_PATH[MALI_MMU_PAGE_FAULT]);
    if (id >= 0) {
        for (const auto * i : MALI_MIDGARD_PAGE_FAULT_INSERT_PAGES) {
            lib::printf_str_t<buffer_size> buf {"ARM_Mali-%s_%s", maliFamilyName, i};
            addCounter(buf, id);
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), MALI_TRC_PNT_PATH[MALI_MMU_PAGE_FAULT]);
        }
    }

    id = getTracepointId_(traceFsConstants, MALI_MMU_TOTAL_ALLOC, MALI_TRC_PNT_PATH[MALI_MMU_TOTAL_ALLOC]);
    if (id >= 0) {
        lib::printf_str_t<buffer_size> buf {"ARM_Mali-%s_%s", maliFamilyName, MALI_MIDGARD_TOTAL_ALLOC_PAGES};
        addCounter(buf, id);
        mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), MALI_TRC_PNT_PATH[MALI_MMU_TOTAL_ALLOC]);
    }

    // for activity counters
    id = getTracepointId_(traceFsConstants, MALI_JOB_SLOT, MALI_TRC_PNT_PATH[MALI_JOB_SLOT]);
    if (id >= 0) {
        lib::printf_str_t<buffer_size> buf {"ARM_Mali-%s_fragment", maliFamilyName};
        addCounter(buf, id);
        mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), MALI_TRC_PNT_PATH[MALI_JOB_SLOT]);
        buf.printf("ARM_Mali-%s_vertex", maliFamilyName);
        addCounter(buf, id);
        mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), MALI_TRC_PNT_PATH[MALI_JOB_SLOT]);
        buf.printf("ARM_Mali-%s_opencl", maliFamilyName);
        addCounter(buf, id);
        mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), MALI_TRC_PNT_PATH[MALI_JOB_SLOT]);
    }
}

void PerfDriver::setupCounter(Counter & counter)
{
    auto * const perfCounter = static_cast<PerfCounter *>(findCounter(counter));
    if (perfCounter == nullptr) {
        counter.setEnabled(false);
        return;
    }

    const auto & optionalEventCode = counter.getEventCode();

    LOG_DEBUG("Configuring perf counter %s with event (0x%" PRIxEventCode ")",
              perfCounter->getName(),
              (optionalEventCode.isValid() ? optionalEventCode.asU64() : 0));

    // Don't use the config from counters XML if it's not set, ex: software counters
    if (optionalEventCode.isValid()) {
        perfCounter->setConfig(optionalEventCode.asU64());
    }
    if (counter.getCount() > 0) {
        // EBS
        perfCounter->setCount(counter.getCount());
    }
    perfCounter->setEnabled(true);
    counter.setKey(perfCounter->getKey());
}

std::optional<CapturedSpe> PerfDriver::setupSpe(int sampleRate, const SpeConfiguration & spe, bool supportsSpev1p2)
{
    for (auto * counter = static_cast<PerfCounter *>(getCounters()); counter != nullptr;
         counter = static_cast<PerfCounter *>(counter->getNext())) {
        if (spe.applies_to_counter(counter->getName(), counter->getPerfEventGroupIdentifier())) {
            uint64_t config = 0;
            uint64_t config1 = 0;
            uint64_t config2 = 0;
            uint64_t config3 = 0;

            SET_SPE_CFG(min_latency, spe.min_latency);
            LOG_DEBUG("Set Spe Event min latency : %d\n ", spe.min_latency);
            const size_t branchCount = spe.ops.count(SpeOps::BRANCH);
            SET_SPE_CFG(branch_filter, branchCount);
            LOG_DEBUG("Set Spe branch ops count : %zu\n ", branchCount);
            const size_t loadCount = spe.ops.count(SpeOps::LOAD);
            SET_SPE_CFG(load_filter, loadCount);
            LOG_DEBUG("Set Spe load ops count : %zu\n ", loadCount);
            const size_t storeCount = spe.ops.count(SpeOps::STORE);
            SET_SPE_CFG(store_filter, storeCount);
            LOG_DEBUG("Set Spe store ops count : %zu\n ", storeCount);

            if (spe.inverse_event_filter_mask && supportsSpev1p2) {
                SET_SPE_CFG(inv_event_filter, spe.event_filter_mask);
                LOG_DEBUG("Set Inverse Spe Event filter mask : 0x%jx\n ", spe.event_filter_mask);
            }
            else {
                if (spe.inverse_event_filter_mask && !supportsSpev1p2) {
                    LOG_WARNING("Spe inverse filter enabled on unsupported device. Ignoring inverse flag.");
                }
                SET_SPE_CFG(event_filter, spe.event_filter_mask);
                LOG_DEBUG("Set Spe Event filter mask : 0x%jx\n ", spe.event_filter_mask);
            }

            // enable timestamps
            SET_SPE_CFG(ts_enable, 1);
            // disable physical addresses as not currently processed
            SET_SPE_CFG(pa_enable, 0);
            // disable physical clock timestamps, use virtual clock timestamps
            SET_SPE_CFG(pct_enable, 0);
            // enable jitter
            SET_SPE_CFG(jitter, 1);

            counter->setConfig(config);
            counter->setConfig1(config1);
            counter->setConfig2(config2);
            counter->setConfig3(config3);

            if (sampleRate < 0) {
                LOG_DEBUG("SPE: Using default sample rate");
                counter->setCount(SPE_DEFAULT_SAMPLE_RATE);
            }
            else {
                LOG_DEBUG("SPE: Using user supplied sample rate %d", sampleRate);
                counter->setCount(sampleRate);
            }
            counter->setEnabled(true);
            LOG_DEBUG("Enabled SPE counter %s %d", counter->getName(), counter->getKey());
            return {{counter->getName(), counter->getKey()}};
        }
    }

    return {};
}

bool PerfDriver::enableGatorTracepoints(IPerfGroups & group, attr_to_key_mapping_tracker_t & mapping_tracker) const
{
    std::int64_t id;

    // enable GATOR TRACEPOINTS
    id = getTracepointId_(traceFsConstants, "gator counter", GATOR_COUNTER);
    if (id >= 0) {
        if (!enableGatorTracePoint(group, mapping_tracker, id)) {
            return false;
        }
    }

    id = getTracepointId_(traceFsConstants, "gator bookmark", GATOR_BOOKMARK);
    if (id >= 0) {
        if (!enableGatorTracePoint(group, mapping_tracker, id)) {
            return false;
        }
    }

    id = getTracepointId_(traceFsConstants, "gator text", GATOR_TEXT);
    if (id >= 0) {
        if (!enableGatorTracePoint(group, mapping_tracker, id)) {
            return false;
        }
    }

    return true;
}

bool PerfDriver::enableTimelineCounters(IPerfGroups & group,
                                        attr_to_key_mapping_tracker_t & mapping_tracker,
                                        std::map<PerfEventGroupIdentifier, std::size_t> & cpu_event_counts,
                                        std::unordered_map<std::string, int> & metric_ids) const
{
    const uint64_t mali_job_slots_id =
        getConfig().can_access_tracepoints
            ? getTracepointId_(traceFsConstants, "Mali: Job slot events", "mali/mali_job_slots_event")
            : 0 /* never used */;

    bool sentMaliJobSlotEvents = false;

    for (auto * counter = static_cast<PerfCounter *>(getCounters()); counter != nullptr;
         counter = static_cast<PerfCounter *>(counter->getNext())) {

        if (!counter->isEnabled()) {
            continue;
        }

        auto const & attr = counter->getAttr();

        if (attr.type == TYPE_METRIC) {
            // accumulate these for filtering later
            metric_ids.try_emplace(counter->getName(), counter->getKey());
        }
        else if (attr.type != TYPE_DERIVED) {
            // do not sent mali_job_slots_event tracepoint multiple times; just send it once and let the processing on the host side
            // deal with multiple counters that are generated from it
            const bool isMaliJobSlotEvents = getConfig().can_access_tracepoints && (attr.type == PERF_TYPE_TRACEPOINT)
                                          && (attr.config == mali_job_slots_id);

            if (isMaliJobSlotEvents && sentMaliJobSlotEvents) {
                continue;
            }

            sentMaliJobSlotEvents |= isMaliJobSlotEvents;

            // count the number of CPU PMU counters used as these will not be available to metrics
            if ((attr.type != PERF_TYPE_SOFTWARE) && (attr.type != PERF_TYPE_TRACEPOINT)) {
                cpu_event_counts[counter->getPerfEventGroupIdentifier()] += 1;
            }

            // add the main event
            if (!group.add(mapping_tracker,
                           counter->getPerfEventGroupIdentifier(),
                           counter->getKey(),
                           counter->getAttr(),
                           counter->usesAux())) {
                LOG_DEBUG("PerfGroups::add failed");
                return false;
            }

            // and the secondary event
            if (counter->hasConfigId2()
                && !group.add(mapping_tracker,
                              counter->getPerfEventGroupIdentifier(),
                              counter->getKey() | 0x40000000,
                              counter->getAttr2(),
                              counter->usesAux())) {
                LOG_DEBUG("PerfGroups::add (2nd) failed");
                return false;
            }
        }
    }

    return true;
}

bool PerfDriver::enable(IPerfGroups & group,
                        attr_to_key_mapping_tracker_t & mapping_tracker,
                        metric_key_to_event_key_tracker_t & metric_tracker) const
{
    std::uint16_t const cpu_cycles_event =
        (mConfig.config.has_armv7_pmu_driver ? armv7PmuDriverCycleCounterPseudoEvent //
                                             : armv7AndLaterClockCyclesEvent);

    std::map<PerfEventGroupIdentifier, std::size_t> cpu_event_counts {};
    std::unordered_map<std::string, int> metric_ids {};

    // prepare the per-cpu group leaders (these collect context switch/forks/exits/mmaps/etc)
    for (const PerfCpu & cluster : mConfig.cpus) {
        PerfEventGroupIdentifier clusterGroupIdentifier(cluster.gator_cpu);
        group.addGroupLeader(mapping_tracker, clusterGroupIdentifier, false);
    }

    // add gatord annotations
    if (!mDisableKernelAnnotations) {
        if (!enableGatorTracepoints(group, mapping_tracker)) {
            return false;
        }
    }

    // add timeline counters
    if (!enableTimelineCounters(group, mapping_tracker, cpu_event_counts, metric_ids)) {
        return false;
    }

    if (!metric_ids.empty()) {
        std::stringstream strm {};
        for (auto const & [key, value] : metric_ids) {
            strm << key << ", " << value << "\n";
        }
        LOG_FINE("Desired metrics:\n%s", strm.str().c_str());
    }

    // enable metrics
    auto const supports_groups_read_format =
        isCaptureOperationModeSupportingCounterGroups(gSessionData.mCaptureOperationMode,
                                                      mConfig.config.supports_inherit_sample_read);
    auto const supports_strobing = (mConfig.config.supports_strobing_core || mConfig.config.supports_strobing_patches);

    auto strobing_mode = (supports_strobing && supports_groups_read_format);

    switch (gSessionData.mMetricSamplingMode) {
        case MetricSamplingMode::strobing:
            if (!strobing_mode) {
                LOG_ERROR("Strobed metrics collection is not supported on this target.");
                return false;
            }
            break;
        case MetricSamplingMode::ebs:
            strobing_mode = false;
            break;
        case MetricSamplingMode::automatic:
            break;
        default:
            LOG_ERROR("Unknown metric sampling mode. should be strobing or ebs");
            return false;
    }

    for (const PerfCpu & cluster : mConfig.cpus) {
        std::string_view const counter_set = cluster.gator_cpu.getCounterSet();
        auto const * cpu_metrics = metrics::find_events_for_cset(counter_set);

        if (cpu_metrics != nullptr) {
            auto const & cpu_metrics_common = get_common_metrics_version(*cpu_metrics);
            auto const [cpu_version, cpu_metrics_for_version] =
                get_specific_metrics_version(*cpu_metrics, cluster.gator_cpu, cpu_metric_versions);
            auto const combined_metrics = combine_metrics(cpu_metrics_common, cpu_version, cpu_metrics_for_version);

            if (!add_metrics_for(group,
                                 mapping_tracker,
                                 metric_tracker,
                                 cpu_event_counts,
                                 metric_ids,
                                 cpu_cycles_event,
                                 strobing_mode,
                                 cluster,
                                 cpu_metrics->return_event_code,
                                 combined_metrics)) {
                return false;
            }
        }
        else {
            LOG_INFO("No metrics set for counter set %s, n_counters=%i",
                     counter_set.data(),
                     cluster.gator_cpu.getPmncCounters());
        }
    }

    return true;
}

void PerfDriver::read(IPerfAttrsConsumer & attrsConsumer, const int cpu)
{
    const GatorCpu * const cluster = mCpuInfo.getCluster(cpu);

    for (auto * counter = static_cast<PerfCounter *>(getCounters()); counter != nullptr;
         counter = static_cast<PerfCounter *>(counter->getNext())) {
        if (!counter->isEnabled()) {
            continue;
        }
        counter->read(attrsConsumer, cpu, cluster);
    }
}

static bool readKernelAnnotateTrcPntFrmt(IPerfAttrsConsumer & attrsConsumer,
                                         const TraceFsConstants & traceFsConstants,
                                         const char * name)
{
    auto id = getTracepointId_(traceFsConstants, name);
    return (id < 0) || readTracepointFormat(attrsConsumer, traceFsConstants, name);
}

bool PerfDriver::sendTracepointFormats(IPerfAttrsConsumer & attrsConsumer)
{
    if (!readTracepointFormat(attrsConsumer, traceFsConstants, SCHED_SWITCH)
        || (!mConfig.config.use_ftrace_for_cpu_frequency
            && !readTracepointFormat(attrsConsumer, traceFsConstants, CPU_FREQUENCY))) {
        return false;
    }

    if (!mDisableKernelAnnotations) {
        if (!readKernelAnnotateTrcPntFrmt(attrsConsumer, traceFsConstants, GATOR_BOOKMARK)
            || !readKernelAnnotateTrcPntFrmt(attrsConsumer, traceFsConstants, GATOR_TEXT)
            || !readKernelAnnotateTrcPntFrmt(attrsConsumer, traceFsConstants, GATOR_COUNTER)) {
            return false;
        }
    }

    for (PerfTracepoint * tracepoint = mTracepoints; tracepoint != nullptr; tracepoint = tracepoint->getNext()) {
        if (tracepoint->getCounter()->isEnabled()
            && !readTracepointFormat(attrsConsumer, traceFsConstants, tracepoint->getTracepoint())) {
            return false;
        }
    }

    return true;
}

// Emits possible dynamically generated events/counters
void PerfDriver::writeEvents(mxml_node_t * root) const
{
    for (const auto & perf_cpu : mConfig.cpus) {
        auto const & gator_cpu = perf_cpu.gator_cpu;
        auto const * cpu_metrics = metrics::find_events_for_cset(gator_cpu.getCounterSet());
        if (cpu_metrics == nullptr) {
            continue;
        }

        auto const & cpu_metrics_common = get_common_metrics_version(*cpu_metrics);
        auto const [cpu_version, cpu_metrics_for_version] =
            get_specific_metrics_version(*cpu_metrics, gator_cpu, cpu_metric_versions);
        auto const combined_metrics = combine_metrics(cpu_metrics_common, cpu_version, cpu_metrics_for_version);

        std::vector<combined_metrics_hierarchy_entry_t> root_events_top_down;
        std::vector<combined_metrics_hierarchy_entry_t> root_events_other;

        for (auto const & entry : combined_metrics.root_events) {
            if (entry.top_down) {
                root_events_top_down.emplace_back(entry);
            }
            else {
                root_events_other.emplace_back(entry);
            }
        }

        if (!root_events_top_down.empty()) {
            writeEventsFor(perf_cpu,
                           root,
                           lib::dyn_printf_str_t("%s: Top Down Metrics", gator_cpu.getCoreName()),
                           cpu_version,
                           root_events_top_down);
        }

        if (!root_events_other.empty()) {
            writeEventsFor(perf_cpu,
                           root,
                           lib::dyn_printf_str_t("%s: Other Metrics", gator_cpu.getCoreName()),
                           cpu_version,
                           root_events_other);
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
void PerfDriver::writeEventsFor(const PerfCpu & perfCpu,
                                mxml_node_t * root,
                                std::string const & category_name,
                                metrics::metric_cpu_version_t const & version,
                                lib::Span<combined_metrics_hierarchy_entry_t const> events)
{
    auto const & gator_cpu = perfCpu.gator_cpu;

    auto * category = mxmlNewElement(root, "category");
    mxmlElementSetAttr(category, "name", category_name.c_str());

    for (auto const & entry : events) {
        auto const & metrics_set = entry.metric.get();

        // only expose metrics that fit in the available PMU counter count
        if ((gator_cpu.getPmncCounters() > 0)
            && (metrics_set.event_codes.size() <= std::size_t(gator_cpu.getPmncCounters()))) {

            auto * node = mxmlNewElement(category, "event");
            auto const group_name = metrics::metric_group_title(entry.group);

            mxmlElementSetAttr(node, "counter", metric_counter_name(perfCpu, version, metrics_set).c_str());
            mxmlElementSetAttr(node, "title", group_name.data());
            mxmlElementSetAttr(node, "name", metrics_set.title.data());
            mxmlElementSetAttr(node, "display", "average");
            mxmlElementSetAttr(node, "class", "delta");
            mxmlElementSetAttr(node, "units", metrics_set.unit.data());
            mxmlElementSetAttr(node, "average_selection", "yes");
            mxmlElementSetAttr(node, "series_composition", "stacked");
            mxmlElementSetAttr(node, "rendering_type", "bar");
            mxmlElementSetAttr(node, "per_cpu", "yes");
            mxmlElementSetAttr(node, "description", metrics_set.description.data());
            mxmlElementSetAttr(node, "metric", "yes");
            mxmlElementSetAttr(node, "metric_uses_cycles", (metrics_set.uses_cycles ? "yes" : "no"));
            mxmlElementSetAttrf(node, "metric_num_events", "%zu", metrics_set.event_codes.size());
            mxmlElementSetAttr(node, "metric_cpu_counter_set", gator_cpu.getCounterSet());
        }

        // recurse to children
        if (!entry.children.empty()) {
            writeEventsFor(perfCpu,
                           root, //
                           lib::Format() << category_name << ": " << /*n << ". " <<*/ metrics_set.title.data(),
                           version,
                           entry.children);
        }
    }
}

int PerfDriver::writeCounters(available_counter_consumer_t const & consumer) const
{
    int count = SimpleDriver::writeCounters(consumer);

    for (const auto & perfCpu : mConfig.cpus) {
        auto const & gator_cpu = perfCpu.gator_cpu;

        // SPE
        const char * const speName = gator_cpu.getSpeName();
        if (speName != nullptr) {
            consumer(counter_type_t::spe, speName);
            ++count;
        }

        // METRICS
        auto const * cpu_metrics = metrics::find_events_for_cset(gator_cpu.getCounterSet());
        if (cpu_metrics != nullptr) {
            auto const & cpu_metrics_common = get_common_metrics_version(*cpu_metrics);
            auto const [cpu_version, cpu_metrics_for_version] =
                get_specific_metrics_version(*cpu_metrics, gator_cpu, cpu_metric_versions);
            auto const combined_metrics = combine_metrics(cpu_metrics_common, cpu_version, cpu_metrics_for_version);

            count += writeCountersFor(perfCpu, combined_metrics, consumer);
        }
    }

    return count;
}

int PerfDriver::writeCountersFor(const PerfCpu & perfCpu,
                                 combined_metrics_t const & cpu_metrics,
                                 available_counter_consumer_t const & consumer)
{
    return writeCountersForRecursive(perfCpu, cpu_metrics.version, cpu_metrics.root_events, consumer);
}

// NOLINTNEXTLINE(misc-no-recursion)
int PerfDriver::writeCountersForRecursive(const PerfCpu & perfCpu,
                                          metrics::metric_cpu_version_t const & version,
                                          lib::Span<combined_metrics_hierarchy_entry_t const> events,
                                          available_counter_consumer_t const & consumer)
{
    int count = 0;
    for (auto const & entry : events) {
        auto const & metrics_set = entry.metric.get();
        consumer(counter_type_t::counter, metric_counter_name(perfCpu, version, metrics_set));

        count = count + 1 + writeCountersForRecursive(perfCpu, version, entry.children, consumer);
    }
    return count;
}

std::vector<agents::perf::perf_capture_configuration_t::cpu_freq_properties_t>
PerfDriver::get_cpu_cluster_keys_for_cpu_frequency_counter()
{
    std::vector<agents::perf::perf_capture_configuration_t::cpu_freq_properties_t> result {};
    for (auto const & cluster : mCpuInfo.getClusters()) {
        int key = 0;
        bool use_cpuinfo = false;
        for (auto * counter = static_cast<PerfCounter *>(getCounters()); counter != nullptr;
             counter = static_cast<PerfCounter *>(counter->getNext())) {
            if (!counter->isEnabled()) {
                continue;
            }
            if (!counter->isCpuFreqCounterFor(cluster)) {
                continue;
            }
            const auto * cpu_freq_counter = static_cast<CPUFreqDriver *>(counter);
            use_cpuinfo = cpu_freq_counter->isUseCpuInfoPath();
            key = cpu_freq_counter->getKey();
            break;
        }
        result.emplace_back(agents::perf::perf_capture_configuration_t::cpu_freq_properties_t {key, use_cpuinfo});
    }
    return result;
}

[[nodiscard]] std::set<std::string_view> PerfDriver::metricsSupporting(
    metrics::metric_group_set_t const & desired) const
{
    std::set<std::string_view> metricIds {};
    DriverCounter * current = getCounters();
    for (; current != nullptr; current = current->getNext()) {
        if (current->supportsAtLeastOne(desired)) {
            metricIds.insert(current->getName());
        }
    }
    return metricIds;
}
