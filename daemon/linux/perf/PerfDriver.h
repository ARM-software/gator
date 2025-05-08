/* Copyright (C) 2013-2024 by Arm Limited. All rights reserved. */

#ifndef PERFDRIVER_H
#define PERFDRIVER_H

#include "IPerfGroups.h"
#include "SimpleDriver.h"
#include "agents/agent_workers_process_holder.h"
#include "agents/perf/capture_configuration.h"
#include "agents/perf/source_adapter.h"
#include "lib/midr.h"
#include "linux/Tracepoints.h"
#include "linux/perf/PerfConfig.h"
#include "linux/perf/PerfDriverConfiguration.h"
#include "linux/perf/metric_key_to_event_key_tracker.h"
#include "metrics/definitions.hpp"

#include <functional>
#include <memory>
#include <set>
#include <unordered_map>

static constexpr const char * SCHED_SWITCH = "sched/sched_switch";
static constexpr const char * CPU_IDLE = "power/cpu_idle";
static constexpr const char * CPU_FREQUENCY = "power/cpu_frequency";

static constexpr const char * GATOR_BOOKMARK = "gator/gator_bookmark";
static constexpr const char * GATOR_COUNTER = "gator/gator_counter";
static constexpr const char * GATOR_TEXT = "gator/gator_text";

class Child;
class GatorCpu;
class IPerfGroups;
class IPerfAttrsConsumer;
class PerfTracepoint;
class UncorePmu;
class ICpuInfo;
class FtraceDriver;
struct TraceFsConstants;

static const char * MALI_MMU_IN_USE = "Mali: MMU address space in use";
static const char * MALI_PM_STATUS = "Mali: PM Status";
static const char * MALI_MMU_PAGE_FAULT = "Mali: MMU page fault insert pages";
static const char * MALI_MMU_TOTAL_ALLOC = "Mali: MMU total alloc pages changed";
static const char * MALI_JOB_SLOT = "Mali: Job slot events";

static std::map<const char *, const char *> MALI_TRC_PNT_PATH = { //
    {MALI_MMU_IN_USE, "mali/mali_mmu_as_in_use"},                 //
    {MALI_PM_STATUS, "mali/mali_mmu_as_released"},                //
    {MALI_MMU_PAGE_FAULT, "mali/mali_page_fault_insert_pages"},   //
    {MALI_MMU_TOTAL_ALLOC, "mali/mali_total_alloc_pages_change"}, //
    {MALI_JOB_SLOT, "mali/mali_job_slots_event"}};

/** Represents a single entry in the combined hierarchy */
struct combined_metrics_hierarchy_entry_t {
    std::reference_wrapper<metrics::metric_events_set_t const> metric;
    std::vector<combined_metrics_hierarchy_entry_t> children;
    metrics::metric_group_id_t group;
    bool top_down;

    combined_metrics_hierarchy_entry_t(metrics::metric_hierarchy_entry_t const & entry)
        : metric(entry.metric), group(entry.group), top_down(entry.top_down)
    {
    }
};

/** Represents root details for the combined hierarchy */
struct combined_metrics_t {
    std::vector<combined_metrics_hierarchy_entry_t> root_events;
    metrics::metric_cpu_version_t version;
    std::size_t largest_metric_event_count = 0;
    std::size_t total_num_events = 0;

    explicit combined_metrics_t(metrics::metric_cpu_version_t const & version) : version(version) {}
};

class PerfDriver : public SimpleDriver {
public:
    PerfDriver(PerfDriverConfiguration && configuration,
               PmuXML && pmuXml,
               const char * maliFamilyName,
               const ICpuInfo & cpuInfo,
               const TraceFsConstants & traceFsConstants,
               bool disableKernelAnnotations = false);
    ~PerfDriver() override;

    // Intentionally undefined
    PerfDriver(const PerfDriver &) = delete;
    PerfDriver & operator=(const PerfDriver &) = delete;
    PerfDriver(PerfDriver &&) = delete;
    PerfDriver & operator=(PerfDriver &&) = delete;

    [[nodiscard]] const PerfConfig & getConfig() const { return mConfig.config; }

    void readEvents(mxml_node_t * xml) override;
    [[nodiscard]] int writeCounters(available_counter_consumer_t const & consumer) const override;
    void writeEvents(mxml_node_t * root) const override;
    void setupCounter(Counter & counter) override;
    [[nodiscard]] std::optional<CapturedSpe> setupSpe(int sampleRate,
                                                      const SpeConfiguration & spe,
                                                      bool supportsSpev1p2) override;
    [[nodiscard]] bool enable(IPerfGroups & group,
                              attr_to_key_mapping_tracker_t & mapping_tracker,
                              metric_key_to_event_key_tracker_t & metric_tracker) const;
    void read(IPerfAttrsConsumer & attrsConsumer, int cpu);
    [[nodiscard]] bool sendTracepointFormats(IPerfAttrsConsumer & attrsConsumer);

    [[nodiscard]] const TraceFsConstants & getTraceFsConstants() const { return traceFsConstants; };

    [[nodiscard]] std::shared_ptr<PrimarySource> create_source(
        sem_t & senderSem,
        ISender & sender,
        std::function<bool()> session_ended_callback,
        std::function<void()> exec_target_app_callback,
        std::function<void()> profilingStartedCallback,
        const std::set<int> & appTids,
        FtraceDriver & ftraceDriver,
        bool enableOnCommandExec,
        ICpuInfo & cpuInfo,
        lib::Span<UncorePmu> uncore_pmus,
        agents::agent_workers_process_default_t & agent_workers_process);

    std::set<std::string_view> metricsSupporting(metrics::metric_group_set_t const & desired) override;

private:
    static void writeEventsFor(const PerfCpu & perfCpu,
                               mxml_node_t * root,
                               std::string const & category_name,
                               metrics::metric_cpu_version_t const & version,
                               lib::Span<combined_metrics_hierarchy_entry_t const> events);

    static int writeCountersFor(const PerfCpu & perfCpu,
                                combined_metrics_t const & cpu_metrics,
                                available_counter_consumer_t const & consumer);

    static int writeCountersForRecursive(const PerfCpu & perfCpu,
                                         metrics::metric_cpu_version_t const & version,
                                         lib::Span<combined_metrics_hierarchy_entry_t const> events,
                                         available_counter_consumer_t const & consumer);

    const TraceFsConstants & traceFsConstants;
    PerfTracepoint * mTracepoints = nullptr;
    PerfDriverConfiguration mConfig;
    PmuXML mPmuXml;
    const ICpuInfo & mCpuInfo;
    std::unordered_map<cpu_utils::cpuid_t, metrics::metric_cpu_version_t> cpu_metric_versions {};
    bool mDisableKernelAnnotations;

    void addCpuCounters(const PerfCpu & perfCpu);
    void addCpuCounterMetrics(const PerfCpu & perfCpu, combined_metrics_t const & cpu_metrics);
    void addCpuCounterMetricsRecursive(const PerfCpu & perfCpu,
                                       metrics::metric_cpu_version_t const & version,
                                       lib::Span<combined_metrics_hierarchy_entry_t const> events);
    void addUncoreCounters(const PerfUncore & uncore);
    void addMidgardHwTracepoints(const char * maliFamilyName);
    [[nodiscard]] bool enableGatorTracepoints(IPerfGroups & group,
                                              attr_to_key_mapping_tracker_t & mapping_tracker) const;
    [[nodiscard]] bool enableTimelineCounters(IPerfGroups & group,
                                              attr_to_key_mapping_tracker_t & mapping_tracker,
                                              std::map<PerfEventGroupIdentifier, std::size_t> & cpu_event_counts,
                                              std::unordered_map<std::string, int> & metric_ids) const;

    std::vector<agents::perf::perf_capture_configuration_t::cpu_freq_properties_t>
    get_cpu_cluster_keys_for_cpu_frequency_counter();

    std::shared_ptr<agents::perf::perf_source_adapter_t> create_source_adapter(
        agents::agent_workers_process_default_t & agent_workers_process,
        sem_t & senderSem,
        ISender & sender,
        std::function<bool()> session_ended_callback,
        std::function<void()> exec_target_app_callback,
        std::function<void()> profiling_started_callback,
        const std::set<int> & app_tids,
        lib::Span<UncorePmu> uncore_pmus,
        const perf_groups_configurer_state_t & perf_groups,
        const agents::perf::buffer_config_t & ringbuffer_config,
        bool enable_on_exec);
};

#endif // PERFDRIVER_H
