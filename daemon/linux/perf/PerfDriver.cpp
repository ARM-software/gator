/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "linux/perf/PerfDriver.h"

#include <sys/wait.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "ISummaryConsumer.h"
#include "Config.h"
#include "ConfigurationXML.h"
#include "Counter.h"
#include "ICpuInfo.h"
#include "Logging.h"
#include "linux/SysfsSummaryInformation.h"
#include "linux/perf/PerfAttrsBuffer.h"
#include "linux/perf/IPerfGroups.h"
#include "linux/perf/PerfEventGroupIdentifier.h"
#include "PmuXML.h"
#include "Tracepoints.h"
#include "SessionData.h"
#include "lib/Assert.h"
#include "lib/Time.h"
#include "lib/Utils.h"
#include "k/perf_event.h"

#define TYPE_DERIVED ~0U

// from driver/perf/arm_spe_pmu.c
// alternatively we could read them from /sys/devices/arm_spe_0/format/*
#define SPE_ts_enable_CFG      config  /* PMSCR_EL1.TS */
#define SPE_ts_enable_LO       0
#define SPE_ts_enable_HI       0
#define SPE_pa_enable_CFG      config  /* PMSCR_EL1.PA */
#define SPE_pa_enable_LO       1
#define SPE_pa_enable_HI       1
#define SPE_pct_enable_CFG     config  /* PMSCR_EL1.PCT */
#define SPE_pct_enable_LO      2
#define SPE_pct_enable_HI      2
#define SPE_jitter_CFG         config  /* PMSIRR_EL1.RND */
#define SPE_jitter_LO          16
#define SPE_jitter_HI          16
#define SPE_branch_filter_CFG      config  /* PMSFCR_EL1.B */
#define SPE_branch_filter_LO       32
#define SPE_branch_filter_HI       32
#define SPE_load_filter_CFG        config  /* PMSFCR_EL1.LD */
#define SPE_load_filter_LO     33
#define SPE_load_filter_HI     33
#define SPE_store_filter_CFG       config  /* PMSFCR_EL1.ST */
#define SPE_store_filter_LO        34
#define SPE_store_filter_HI        34

#define SPE_event_filter_CFG       config1 /* PMSEVFR_EL1 */
#define SPE_event_filter_LO        0
#define SPE_event_filter_HI        63

#define SPE_min_latency_CFG        config2 /* PMSLATFR_EL1.MINLAT */
#define SPE_min_latency_LO     0
#define SPE_min_latency_HI     11

// An improved version would mask out old value be we assume 0
#define SET_SPE_CFG(cfg, value) SPE_##cfg##_CFG |= static_cast<uint64_t>(value) << SPE_##cfg##_LO



static constexpr uint64_t armv7AndLaterClockCyclesEvent = 0x11;
static constexpr uint64_t armv7PmuDriverCycleCounterPsuedoEvent = 0xFF;

class PerfCounter : public DriverCounter
{
public:
    static constexpr uint64_t noConfigId2 = ~0ull;

    PerfCounter(DriverCounter *next, const PerfEventGroupIdentifier & groupIdentifier, const char *name, const IPerfGroups::Attr & attr, bool usesAux, uint64_t config_id2 = noConfigId2,
                bool fixUpClockCyclesEvent = false)
            : DriverCounter(next, name),
              eventGroupIdentifier(groupIdentifier),
              attr(attr),
              mConfigId2(config_id2),
              mFixUpClockCyclesEvent(fixUpClockCyclesEvent),
              mUsesAux(usesAux)
    {
    }

    PerfCounter(DriverCounter *next, const PerfEventGroupIdentifier & groupIdentifier, const char *name, uint32_t type,
                uint64_t config, uint64_t sampleType, uint64_t count, uint64_t config_id2 = noConfigId2,
                bool fixUpClockCyclesEvent = false)
            : PerfCounter(next, groupIdentifier, name, IPerfGroups::Attr {}, false,
                          config_id2, fixUpClockCyclesEvent)
    {
        attr.type = type;
        attr.config = config;
        attr.periodOrFreq = count;
        attr.sampleType = sampleType;
    }

    /**
     *
     * @param
     * @param cpu
     * @param cluster maybe null if unknown
     */
    virtual void read(IPerfAttrsConsumer &, const int /* cpu */, const GatorCpu * /* cluster */)
    {
    }

    inline const PerfEventGroupIdentifier & getPerfEventGroupIdentifier() const
    {
        return eventGroupIdentifier;
    }

    const IPerfGroups::Attr getAttr() const {
        return attr;
    }

    inline bool hasConfigId2() const
    {
        return mConfigId2 != noConfigId2;
    }

    inline bool usesAux() const
    {
        return mUsesAux;
    }

    IPerfGroups::Attr getAttr2() const
    {
        IPerfGroups::Attr attr2 {attr};
        attr2.config = mConfigId2;
        return attr2;
    }

    inline void setCount(const uint64_t count)
    {
        attr.periodOrFreq = count;
    }

    inline void setConfig(const uint64_t config)
    {
        // The Armv7 PMU driver in the linux kernel uses a special event number for the cycle counter
        // that is different from the clock cycles event number.
        // https://github.com/torvalds/linux/blob/0adb32858b0bddf4ada5f364a84ed60b196dbcda/arch/arm/kernel/perf_event_v7.c#L1042
        if (mFixUpClockCyclesEvent && config == armv7AndLaterClockCyclesEvent)
            attr.config = armv7PmuDriverCycleCounterPsuedoEvent;
        else
            attr.config = config;
    }

    inline void setConfig1(const uint64_t config)
    {
        attr.config1 = config;
    }

    inline void setConfig2(const uint64_t config)
    {
        attr.config2 = config;
    }

    inline void setSampleType(uint64_t sampleType)
    {
        attr.sampleType = sampleType;
    }
private:

    const PerfEventGroupIdentifier eventGroupIdentifier;
    IPerfGroups::Attr attr;
    const uint64_t mConfigId2;
    bool mFixUpClockCyclesEvent;
    bool mUsesAux;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(PerfCounter);
};

class CPUFreqDriver : public PerfCounter
{
public:
    CPUFreqDriver(DriverCounter *next, const char *name, uint64_t id, const GatorCpu & cluster)
            : PerfCounter(next, PerfEventGroupIdentifier(cluster),
                          name, PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_RAW, 1)
    {
    }

    void read(IPerfAttrsConsumer & attrsConsumer, const int cpu, const GatorCpu * cluster)
    {
        if (cluster == nullptr || !(*cluster == *getPerfEventGroupIdentifier().getCluster())) {
            return;
        }

        char buf[64];

        snprintf(buf, sizeof(buf), "/sys/devices/system/cpu/cpu%i/cpufreq/cpuinfo_cur_freq", cpu);
        int64_t freq;
        if (lib::readInt64FromFile(buf, freq) != 0) {
            freq = 0;
        }
        attrsConsumer.perfCounter(cpu, getKey(), 1000 * freq);
    }

private:

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(CPUFreqDriver);
};

template <typename T>
inline static T & neverNull(T * t)
{
    if (t == nullptr) {
        handleException();
    }
    return *t;
}

static long long getTracepointId(const char * const counter, const char * const name)
{
    long long result = getTracepointId(name);
    if (result <= 0) {
        logg.logSetup("%s is disabled\n%s was not found", counter, getTracepointPath(name, "id").c_str());
    }
    return result;
}


PerfDriver::PerfDriver(PerfDriverConfiguration && configuration, PmuXML && pmuXml, const char * maliFamilyName, const ICpuInfo & cpuInfo)
        : SimpleDriver("Perf"),
          mTracepoints(nullptr),
          mIsSetup(false),
          mConfig(std::move(configuration)),
          mPmuXml(pmuXml),
          mCpuInfo(cpuInfo)
{
    // add CPU PMUs
    for (const auto & perfCpu : mConfig.cpus) {

        if (perfCpu.gator_cpu.getCpuid() != PerfDriverConfiguration::UNKNOWN_CPUID) {
            if (perfCpu.pmu_type == PERF_TYPE_RAW) {
                logg.logMessage("Adding cpu counters (based on cpuid) for %s", perfCpu.gator_cpu.getCoreName());
            }
            else {
                logg.logMessage("Adding cpu counters for %s with type %i", perfCpu.gator_cpu.getCoreName(), perfCpu.pmu_type);
            }
        }
        else {
            logg.logMessage("Adding cpu counters based on default CPU object");
        }

        addCpuCounters(perfCpu);
    }

    // add uncore PMUs
    for (const auto & perfUncore : mConfig.uncores) {
        logg.logMessage("Adding uncore counters for %s with type %i", perfUncore.uncore_pmu.getCoreName(), perfUncore.pmu_type);
        addUncoreCounters(perfUncore);
    }

    // Add supported software counters
    long long id;
    char buf[40];

    if (getConfig().can_access_tracepoints)
    {
        id = getTracepointId("Interrupts: SoftIRQ", "irq/softirq_exit");
        if (id >= 0) {
            for (const auto & perfCpu : mConfig.cpus) {
                snprintf(buf, sizeof(buf), "%s_softirq", perfCpu.gator_cpu.getPmncName());
                setCounters(
                        new PerfCounter(getCounters(), PerfEventGroupIdentifier(perfCpu.gator_cpu),
                                        buf, PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_READ,
                                        0));
            }
        }

        id = getTracepointId("Interrupts: IRQ", "irq/irq_handler_exit");
        if (id >= 0) {
            for (const auto & perfCpu : mConfig.cpus) {
                snprintf(buf, sizeof(buf), "%s_irq", perfCpu.gator_cpu.getPmncName());
                setCounters(
                        new PerfCounter(getCounters(), PerfEventGroupIdentifier(perfCpu.gator_cpu),
                                        buf, PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_READ,
                                        0));
            }
        }

        id = getTracepointId("Scheduler: Switch", SCHED_SWITCH);
        if (id >= 0) {
            for (const auto & perfCpu : mConfig.cpus) {
                snprintf(buf, sizeof(buf), "%s_switch", perfCpu.gator_cpu.getPmncName());
                setCounters(
                        new PerfCounter(getCounters(), PerfEventGroupIdentifier(perfCpu.gator_cpu),
                                        buf, PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_READ,
                                        0));
            }
        }

        id = getTracepointId("Clock: Frequency", CPU_FREQUENCY);
        if (id >= 0 && access("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq", R_OK) == 0) {
            for (const auto & perfCpu : mConfig.cpus) {
                snprintf(buf, sizeof(buf), "%s_freq", perfCpu.gator_cpu.getPmncName());
                setCounters(
                        new CPUFreqDriver(getCounters(), buf, id, perfCpu.gator_cpu));
            }
        }
    }

    if (getConfig().can_access_tracepoints || getConfig().has_attr_context_switch) {
        setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), "Linux_cpu_wait_contention", TYPE_DERIVED, -1, 0, 0));
        for (const auto & perfCpu : mConfig.cpus) {
            snprintf(buf, sizeof(buf), "%s_system", perfCpu.gator_cpu.getPmncName());
            setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(perfCpu.gator_cpu),
                                        buf, TYPE_DERIVED, -1, 0, 0));
            snprintf(buf, sizeof(buf), "%s_user", perfCpu.gator_cpu.getPmncName());
            setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(perfCpu.gator_cpu),
                                        buf, TYPE_DERIVED, -1, 0, 0));
        }
    }

    // add
    if (maliFamilyName != NULL) {
        // add midgard software tracepoints
        addMidgardHwTracepoints(maliFamilyName);
    }

    //Adding for performance counters for perf software
    setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), "PERF_COUNT_SW_CPU_CLOCK", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK, 0, 0));
    setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), "PERF_COUNT_SW_TASK_CLOCK", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK, 0, 0));
    setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), "PERF_COUNT_SW_CONTEXT_SWITCHES", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES, 0, 0));
    setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), "PERF_COUNT_SW_CPU_MIGRATIONS", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_MIGRATIONS, 0, 0));
    setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), "PERF_COUNT_SW_PAGE_FAULTS", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS, 0, 0));
    setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), "PERF_COUNT_SW_PAGE_FAULTS_MAJ", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MAJ, 0, 0));
    setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), "PERF_COUNT_SW_PAGE_FAULTS_MIN", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MIN, 0, 0));
    setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), "PERF_COUNT_SW_ALIGNMENT_FAULTS", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_ALIGNMENT_FAULTS, 0, 0));
    setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), "PERF_COUNT_SW_EMULATION_FAULTS", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_EMULATION_FAULTS, 0, 0));

    mIsSetup = true;
}

class PerfTracepoint
{
public:
    PerfTracepoint(PerfTracepoint * const next, const DriverCounter * const counter, const char * const tracepoint)
            : mNext(next),
              mCounter(counter),
              mTracepoint(tracepoint)
    {
    }

    PerfTracepoint *getNext() const
    {
        return mNext;
    }
    const DriverCounter *getCounter() const
    {
        return mCounter;
    }
    const char *getTracepoint() const
    {
        return mTracepoint.c_str();
    }

private:
    PerfTracepoint * const mNext;
    const DriverCounter * const mCounter;
    const std::string mTracepoint;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(PerfTracepoint)
    ;
};

PerfDriver::~PerfDriver()
{
    for (PerfTracepoint* nextTracepoint = mTracepoints; nextTracepoint != nullptr;) {
        PerfTracepoint* tracepoint = nextTracepoint;
        nextTracepoint = tracepoint->getNext();
        delete tracepoint;
    }
}


void PerfDriver::addCpuCounters(const PerfCpu & perfCpu)
{
    const GatorCpu & cpu = perfCpu.gator_cpu;
    const int type = perfCpu.pmu_type;

    {
        const int len = snprintf(NULL, 0, "%s_ccnt", cpu.getPmncName()) + 1;
        const std::unique_ptr<char[]> name { new char[len] };
        snprintf(name.get(), len, "%s_ccnt", cpu.getPmncName());
        setCounters(
                new PerfCounter(getCounters(), PerfEventGroupIdentifier(cpu), name.get(), type, -1, PERF_SAMPLE_READ, 0,
                                PerfCounter::noConfigId2, getConfig().has_armv7_pmu_driver));
    }

    for (int j = 0; j < cpu.getPmncCounters(); ++j) {
        const int len = snprintf(NULL, 0, "%s_cnt%d", cpu.getPmncName(), j) + 1;
        const std::unique_ptr<char[]> name { new char[len] };
        snprintf(name.get(), len, "%s_cnt%d", cpu.getPmncName(), j);
        setCounters(
                new PerfCounter(getCounters(), PerfEventGroupIdentifier(cpu),
                                name.get(), type, -1, PERF_SAMPLE_READ,
                                0));
    }

    const char * speId = cpu.getSpeName();
    if (speId != nullptr) {
        IPerfGroups::Attr attr;
        attr.sampleType |= PERF_SAMPLE_TID;
        setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(mConfig.cpuNumberToSpeType), speId, attr, true));
    }
}

void PerfDriver::addUncoreCounters(const PerfUncore & perfUncore)
{
    const UncorePmu & pmu = perfUncore.uncore_pmu;
    const int type = perfUncore.pmu_type;

    if (pmu.getHasCyclesCounter()) {
        const int len = snprintf(NULL, 0, "%s_ccnt", pmu.getCoreName()) + 1;
        const std::unique_ptr<char[]> name { new char[len] };
        snprintf(name.get(), len, "%s_ccnt", pmu.getCoreName());
        setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(pmu),
                                    name.get(), type, -1, PERF_SAMPLE_READ, 0));
    }

    for (int j = 0; j < pmu.getPmncCounters(); ++j) {
        const int len = snprintf(NULL, 0, "%s_cnt%d", pmu.getCoreName(), j) + 1;
        const std::unique_ptr<char[]> name { new char[len] };
        snprintf(name.get(), len, "%s_cnt%d", pmu.getCoreName(), j);
        setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(pmu),
                                    name.get(), type, -1, PERF_SAMPLE_READ, 0));
    }
}

void PerfDriver::readEvents(mxml_node_t * const xml)
{
    mxml_node_t *node = xml;

    // Only for use with perf
    if (!mIsSetup || !getConfig().can_access_tracepoints) {
        return;
    }

    while (true) {
        node = mxmlFindElement(node, xml, "event", NULL, NULL, MXML_DESCEND);
        if (node == NULL) {
            break;
        }
        const char *counter = mxmlElementGetAttr(node, "counter");
        if (counter == NULL) {
            continue;
        }

        if (strncmp(counter, "ftrace_", 7) != 0) {
            continue;
        }

        const char *tracepoint = mxmlElementGetAttr(node, "tracepoint");
        if (tracepoint == NULL) {
            const char *regex = mxmlElementGetAttr(node, "regex");
            if (regex == NULL) {
                logg.logError("The tracepoint counter %s is missing the required tracepoint attribute", counter);
                handleException();
            }
            else {
                logg.logMessage("Not using perf for counter %s", counter);
                continue;
            }
        }

        const char *arg = mxmlElementGetAttr(node, "arg");

        long long id = getTracepointId(counter, tracepoint);
        if (id >= 0) {
            logg.logMessage("Using perf for %s", counter);
            setCounters(
                    new PerfCounter(getCounters(), PerfEventGroupIdentifier(),
                                    counter, PERF_TYPE_TRACEPOINT, id,
                                    arg == NULL ? 0 : PERF_SAMPLE_RAW,
                                    1));
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), tracepoint);
        }
    }
}

#define COUNT_OF(X) (sizeof(X) / sizeof(X[0]))

void PerfDriver::addMidgardHwTracepoints(const char * const maliFamilyName)
{
    if (!getConfig().can_access_tracepoints)
        return;

    static const char * const MALI_MIDGARD_AS_IN_USE_RELEASED[] = { "MMU_AS_0", "MMU_AS_1", "MMU_AS_2", "MMU_AS_3" };

    static const char * const MALI_MIDGARD_PAGE_FAULT_INSERT_PAGES[] = { "MMU_PAGE_FAULT_0", "MMU_PAGE_FAULT_1",
                                                                         "MMU_PAGE_FAULT_2", "MMU_PAGE_FAULT_3" };

    static const char * const MALI_MIDGARD_TOTAL_ALLOC_PAGES = "TOTAL_ALLOC_PAGES";

    long long id;
    char buf[256];

    // add midgard software tracepoints
    auto addCounterWithConfigId2 = [this] (const char * name, uint64_t id, uint64_t configId2) {
        IPerfGroups::Attr attr {};
        attr.type = PERF_TYPE_TRACEPOINT;
        attr.config = id;
        attr.periodOrFreq = 1;
        attr.sampleType = PERF_SAMPLE_RAW;
        attr.task = true;
        setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), name, attr, false, configId2, false));
    };
    auto addCounter = [addCounterWithConfigId2] (const char * name, uint64_t id) {
        addCounterWithConfigId2(name, id, PerfCounter::noConfigId2);
    };

    if (false) { /* These are disabled because they never generate events */
        static const char * const MALI_MIDGARD_PM_STATUS_EVENTS[] = { "PM_SHADER_0", "PM_SHADER_1", "PM_SHADER_2",
                                                                      "PM_SHADER_3", "PM_SHADER_4", "PM_SHADER_5",
                                                                      "PM_SHADER_6", "PM_SHADER_7", "PM_TILER_0",
                                                                      "PM_L2_0", "PM_L2_1" };

        id = getTracepointId("Mali: PM Status", "mali/mali_pm_status");
        if (id >= 0) {
            for (size_t i = 0; i < COUNT_OF(MALI_MIDGARD_PM_STATUS_EVENTS); ++i) {
                snprintf(buf, sizeof(buf), "ARM_Mali-%s_%s", maliFamilyName, MALI_MIDGARD_PM_STATUS_EVENTS[i]);
                addCounter(buf, id);
                mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), "mali/mali_pm_status");
            }
        }
    }

    id = getTracepointId("Mali: MMU address space in use", "mali/mali_mmu_as_in_use");
    if (id >= 0) {
        const int id2 = getTracepointId("Mali: PM Status", "mali/mali_mmu_as_released");
        for (size_t i = 0; i < COUNT_OF(MALI_MIDGARD_PAGE_FAULT_INSERT_PAGES); ++i) {
            snprintf(buf, sizeof(buf), "ARM_Mali-%s_%s", maliFamilyName, MALI_MIDGARD_AS_IN_USE_RELEASED[i]);
            addCounterWithConfigId2(buf, id, id2);
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), "mali/mali_mmu_as_in_use");
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), "mali/mali_mmu_as_released");
        }
    }

    id = getTracepointId("Mali: MMU page fault insert pages", "mali/mali_page_fault_insert_pages");
    if (id >= 0) {
        for (size_t i = 0; i < COUNT_OF(MALI_MIDGARD_PAGE_FAULT_INSERT_PAGES); ++i) {
            snprintf(buf, sizeof(buf), "ARM_Mali-%s_%s", maliFamilyName, MALI_MIDGARD_PAGE_FAULT_INSERT_PAGES[i]);
            addCounter(buf, id);
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), "mali/mali_page_fault_insert_pages");
        }
    }

    id = getTracepointId("Mali: MMU total alloc pages changed", "mali/mali_total_alloc_pages_change");
    if (id >= 0) {
        snprintf(buf, sizeof(buf), "ARM_Mali-%s_%s", maliFamilyName, MALI_MIDGARD_TOTAL_ALLOC_PAGES);
        addCounter(buf, id);
        mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), "mali/mali_total_alloc_pages_change");
    }

    // for activity counters
    id = getTracepointId("Mali: Job slot events", "mali/mali_job_slots_event");
    if (id >= 0) {
        snprintf(buf, sizeof(buf), "ARM_Mali-%s_fragment", maliFamilyName);
        addCounter(buf, id);
        mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), "mali/mali_job_slots_event");
        snprintf(buf, sizeof(buf), "ARM_Mali-%s_vertex", maliFamilyName);
        addCounter(buf, id);
        mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), "mali/mali_job_slots_event");
        snprintf(buf, sizeof(buf), "ARM_Mali-%s_opencl", maliFamilyName);
        addCounter(buf, id);
        mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), "mali/mali_job_slots_event");
    }
}

bool PerfDriver::summary(ISummaryConsumer & consumer, std::function<uint64_t()> getAndSetMonotonicStarted)
{
    struct utsname utsname;
    if (uname(&utsname) != 0) {
        logg.logMessage("uname failed");
        return false;
    }

    char buf[512];
    snprintf(buf, sizeof(buf), "%s %s %s %s %s GNU/Linux", utsname.sysname, utsname.nodename, utsname.release,
             utsname.version, utsname.machine);

    long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize < 0) {
        logg.logMessage("sysconf _SC_PAGESIZE failed");
        return false;
    }

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        logg.logMessage("clock_gettime failed");
        return false;
    }

    const int64_t timestamp = ts.tv_sec * NS_PER_S + ts.tv_nsec;

    const uint64_t monotonicStarted = getAndSetMonotonicStarted();
    const uint64_t currTime = 0; //getTime() - gSessionData.mMonotonicStarted;

    std::map<std::string, std::string> additionalAttributes;

    additionalAttributes["perf.is_root"] = (geteuid() == 0 ? "1" : "0");
    additionalAttributes["perf.is_system_wide"] = (getConfig().is_system_wide ? "1" : "0");
    additionalAttributes["perf.can_access_tracepoints"] = (getConfig().can_access_tracepoints ? "1" : "0");
    additionalAttributes["perf.has_attr_context_switch"] = (getConfig().has_attr_context_switch ? "1" : "0");

    lnx::addDefaultSysfsSummaryInformation(additionalAttributes);

    consumer.summary(currTime, timestamp, monotonicStarted, monotonicStarted, buf, pageSize, getConfig().has_attr_clockid_support, additionalAttributes);

    for (size_t i = 0; i < mCpuInfo.getCpuIds().size(); ++i) {
        coreName(currTime, consumer, i);
    }

    consumer.commit(currTime);

    return true;
}

void PerfDriver::coreName(const uint64_t currTime, ISummaryConsumer & consumer, const int cpu)
{
    // Don't send information on a cpu we know nothing about
    const int cpuId = mCpuInfo.getCpuIds()[cpu];
    if (cpuId == -1) {
        return;
    }

    // we use PmuXml here for look up rather than clusters because it maybe a cluster
    // that wasn't known at start up
    const GatorCpu * gatorCpu = mPmuXml.findCpuById(cpuId);
    if (gatorCpu != nullptr) {
        consumer.coreName(currTime, cpu, cpuId, gatorCpu->getCoreName());
    }
    else {
        char buf[32];
        snprintf(buf, sizeof(buf), "Unknown (0x%.3x)", cpuId);
        consumer.coreName(currTime, cpu, cpuId, buf);
    }
}

void PerfDriver::setupCounter(Counter &counter)
{
    PerfCounter * const perfCounter = static_cast<PerfCounter *>(findCounter(counter));
    if (perfCounter == NULL) {
        counter.setEnabled(false);
        return;
    }

    logg.logMessage("Configuring perf counter %s with event (%d)", perfCounter->getName(), counter.getEvent());

    // Don't use the config from counters XML if it's not set, ex: software counters
    if (counter.getEvent() != -1) {
        perfCounter->setConfig(counter.getEvent());
    }
    if (counter.getCount() > 0) {
        // EBS
        perfCounter->setCount(counter.getCount());
    }
    perfCounter->setEnabled(true);
    counter.setKey(perfCounter->getKey());
}

lib::Optional<CapturedSpe> PerfDriver::setupSpe(const SpeConfiguration & spe)
{
    for (PerfCounter *counter = static_cast<PerfCounter *>(getCounters()); counter != NULL;
            counter = static_cast<PerfCounter *>(counter->getNext())) {
        if (spe.id == counter->getName()) {
            uint64_t config = 0;
            uint64_t config1 = 0;
            uint64_t config2 = 0;

            SET_SPE_CFG(event_filter, spe.event_filter_mask);
            SET_SPE_CFG(min_latency, spe.min_latency);
            SET_SPE_CFG(branch_filter, spe.ops.count(SpeOps::BRANCH));
            SET_SPE_CFG(load_filter, spe.ops.count(SpeOps::LOAD));
            SET_SPE_CFG(store_filter, spe.ops.count(SpeOps::STORE));

            // enable timestamps
            SET_SPE_CFG(ts_enable, 1);
            // disable physical addresses as not currently processed
            SET_SPE_CFG(pa_enable, 0);
            // disable physical clock timestamps, use virtual clock timestamps
            SET_SPE_CFG(pct_enable, 0);
            // no jitter
            SET_SPE_CFG(jitter, 0);

            counter->setConfig(config);
            counter->setConfig1(config1);
            counter->setConfig2(config2);
            counter->setCount(1);
            counter->setEnabled(true);
            return { {spe.id, counter->getKey()}};
        }
    }

    return {};
}

bool PerfDriver::enable(const uint64_t currTime, IPerfGroups & group, IPerfAttrsConsumer & attrsConsumer) const
{
    const uint64_t id = getConfig().can_access_tracepoints ? getTracepointId("Mali: Job slot events", "mali/mali_job_slots_event") : 0 /* never used */;
    bool sentMaliJobSlotEvents = false;

    for (const PerfCpu & cluster : mConfig.cpus) {
        PerfEventGroupIdentifier clusterGroupIdentifier(cluster.gator_cpu);
        group.addGroupLeader(currTime, attrsConsumer, clusterGroupIdentifier);
    }

    for (PerfCounter *counter = static_cast<PerfCounter *>(getCounters()); counter != NULL;
            counter = static_cast<PerfCounter *>(counter->getNext()))
    {
        if (counter->isEnabled() && (counter->getAttr().type != TYPE_DERIVED)) {
            // do not sent mali_job_slots_event tracepoint multiple times; just send it once and let the processing on the host side
            // deal with multiple counters that are generated from it
            const bool isMaliJobSlotEvents = getConfig().can_access_tracepoints && (counter->getAttr().type == PERF_TYPE_TRACEPOINT) &&
                                             (counter->getAttr().config == id);
            const bool skip = (isMaliJobSlotEvents && sentMaliJobSlotEvents);

            sentMaliJobSlotEvents |= isMaliJobSlotEvents;

            if (!skip) {
                if (group.add(currTime, attrsConsumer, counter->getPerfEventGroupIdentifier(), counter->getKey(), counter->getAttr(), counter->usesAux())) {
                    if (counter->hasConfigId2()) {
                        if (!group.add(currTime, attrsConsumer, counter->getPerfEventGroupIdentifier(),
                                       counter->getKey() | 0x40000000, counter->getAttr2(), counter->usesAux())) {
                            logg.logMessage("PerfGroups::add (2nd) failed");
                            return false;
                        }
                    }
                }
                else {
                    logg.logMessage("PerfGroups::add failed");
                    return false;
                }
            }
        }
    }

    return true;
}

void PerfDriver::read(IPerfAttrsConsumer & attrsConsumer, const int cpu)
{
    const GatorCpu * const cluster = mCpuInfo.getCluster(cpu);

    for (PerfCounter *counter = static_cast<PerfCounter *>(getCounters()); counter != NULL;
            counter = static_cast<PerfCounter *>(counter->getNext())) {
        if (!counter->isEnabled()) {
            continue;
        }
        counter->read(attrsConsumer, cpu, cluster);
    }
}

bool PerfDriver::sendTracepointFormats(const uint64_t currTime, IPerfAttrsConsumer & attrsConsumer)
{
    if (!readTracepointFormat(currTime, attrsConsumer, SCHED_SWITCH)
            || !readTracepointFormat(currTime, attrsConsumer, CPU_IDLE)
            || !readTracepointFormat(currTime, attrsConsumer, CPU_FREQUENCY) || false) {
        return false;
    }

    for (PerfTracepoint *tracepoint = mTracepoints; tracepoint != NULL; tracepoint = tracepoint->getNext()) {
        if (tracepoint->getCounter()->isEnabled()
                && !readTracepointFormat(currTime, attrsConsumer, tracepoint->getTracepoint())) {
            return false;
        }
    }

    return true;
}

int PerfDriver::writeCounters(mxml_node_t *root) const
{
    int count = SimpleDriver::writeCounters(root);
    for (const auto & perfCpu : mConfig.cpus) {
        const char * const speName = perfCpu.gator_cpu.getSpeName();
        if (speName != nullptr) {
            mxml_node_t *node = mxmlNewElement(root, "spe");
            mxmlElementSetAttr(node, "id", speName);
            ++count;
        }
    }

    return count;
}
