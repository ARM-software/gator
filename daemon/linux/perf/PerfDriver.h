/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef PERFDRIVER_H
#define PERFDRIVER_H

#include "SimpleDriver.h"
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

class ISummaryConsumer;
class GatorCpu;
class IPerfGroups;
class IPerfAttrsConsumer;
class PerfTracepoint;
class UncorePmu;
class ICpuInfo;

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
               const ICpuInfo & cpuInfo);
    ~PerfDriver() override;

    const PerfConfig & getConfig() const { return mConfig.config; }

    void readEvents(mxml_node_t * xml) override;
    int writeCounters(mxml_node_t * root) const override;
    bool summary(ISummaryConsumer & consumer, const std::function<uint64_t()> & getAndSetMonotonicStarted);
    void coreName(uint64_t currTime, ISummaryConsumer & consumer, int cpu);
    void setupCounter(Counter & counter) override;
    lib::Optional<CapturedSpe> setupSpe(int sampleRate, const SpeConfiguration & spe) override;
    bool enable(uint64_t currTime, IPerfGroups & group, IPerfAttrsConsumer & attrsConsumer) const;
    void read(IPerfAttrsConsumer & attrsConsumer, int cpu);
    bool sendTracepointFormats(uint64_t currTime, IPerfAttrsConsumer & attrsConsumer);

private:
    void addCpuCounters(const PerfCpu & cpu);
    void addUncoreCounters(const PerfUncore & uncore);
    PerfTracepoint * mTracepoints;
    PerfDriverConfiguration mConfig;
    PmuXML mPmuXml;
    const ICpuInfo & mCpuInfo;

    // Intentionally undefined
    PerfDriver(const PerfDriver &) = delete;
    PerfDriver & operator=(const PerfDriver &) = delete;
    PerfDriver(PerfDriver &&) = delete;
    PerfDriver & operator=(PerfDriver &&) = delete;

    void addMidgardHwTracepoints(const char * maliFamilyName);
};

#endif // PERFDRIVER_H
