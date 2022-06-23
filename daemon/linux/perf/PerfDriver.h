/* Copyright (C) 2013-2022 by Arm Limited. All rights reserved. */

#ifndef PERFDRIVER_H
#define PERFDRIVER_H

#include "IPerfGroups.h"
#include "SimpleDriver.h"
#include "agents/agent_workers_process.h"
#include "agents/perf/capture_configuration.h"
#include "agents/perf/source_adapter.h"
#include "linux/Tracepoints.h"
#include "linux/perf/PerfConfig.h"
#include "linux/perf/PerfDriverConfiguration.h"

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <set>

static constexpr const char * SCHED_SWITCH = "sched/sched_switch";
static constexpr const char * CPU_IDLE = "power/cpu_idle";
static constexpr const char * CPU_FREQUENCY = "power/cpu_frequency";

static constexpr const char * GATOR_BOOKMARK = "gator/gator_bookmark";
static constexpr const char * GATOR_COUNTER = "gator/gator_counter";
static constexpr const char * GATOR_TEXT = "gator/gator_text";

class Child;
class ISummaryConsumer;
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

    const PerfConfig & getConfig() const { return mConfig.config; }

    void readEvents(mxml_node_t * xml) override;
    int writeCounters(mxml_node_t * root) const override;
    std::optional<std::uint64_t> summary(ISummaryConsumer & consumer,
                                         const std::function<uint64_t()> & getMonotonicTime);
    void coreName(ISummaryConsumer & consumer, int cpu);
    void setupCounter(Counter & counter) override;
    std::optional<CapturedSpe> setupSpe(int sampleRate, const SpeConfiguration & spe) override;
    bool enable(IPerfGroups & group, attr_to_key_mapping_tracker_t & mapping_tracker) const;
    void read(IPerfAttrsConsumer & attrsConsumer, int cpu);
    bool sendTracepointFormats(IPerfAttrsConsumer & attrsConsumer);

    const TraceFsConstants & getTraceFsConstants() const { return traceFsConstants; };

    std::unique_ptr<PrimarySource> create_source(sem_t & senderSem,
                                                 ISender & sender,
                                                 std::function<bool()> session_ended_callback,
                                                 std::function<void()> exec_target_app_callback,
                                                 std::function<void()> profilingStartedCallback,
                                                 const std::set<int> & appTids,
                                                 FtraceDriver & ftraceDriver,
                                                 bool enableOnCommandExec,
                                                 ICpuInfo & cpuInfo,
                                                 lib::Span<UncorePmu> uncore_pmus,
                                                 agents::agent_workers_process_t<Child> & agent_workers_process);

private:
    const TraceFsConstants & traceFsConstants;
    PerfTracepoint * mTracepoints;
    PerfDriverConfiguration mConfig;
    PmuXML mPmuXml;
    const ICpuInfo & mCpuInfo;
    bool mDisableKernelAnnotations;

    void addCpuCounters(const PerfCpu & cpu);
    void addUncoreCounters(const PerfUncore & uncore);
    void addMidgardHwTracepoints(const char * maliFamilyName);
    bool enableGatorTracePoint(IPerfGroups & group,
                               attr_to_key_mapping_tracker_t & mapping_tracker,
                               long long id) const;

    std::vector<agents::perf::perf_capture_configuration_t::cpu_freq_properties_t>
    get_cpu_cluster_keys_for_cpu_frequency_counter();

    std::unique_ptr<agents::perf::perf_source_adapter_t> create_source_adapter(
        agents::agent_workers_process_t<Child> & agent_workers_process,
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
