/* Copyright (C) 2013-2022 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfDriver.h"

#include "Config.h"
#include "ConfigurationXML.h"
#include "Counter.h"
#include "GetEventKey.h"
#include "ICpuInfo.h"
#include "ISummaryConsumer.h"
#include "Logging.h"
#include "SessionData.h"
#include "Tracepoints.h"
#include "agents/perf/perf_driver_summary.h"
#include "k/perf_event.h"
#include "lib/Assert.h"
#include "lib/String.h"
#include "lib/Time.h"
#include "lib/Utils.h"
#include "linux/SysfsSummaryInformation.h"
#include "linux/perf/IPerfGroups.h"
#include "linux/perf/PerfAttrsBuffer.h"
#include "linux/perf/PerfEventGroupIdentifier.h"
#include "linux/perf/PerfSource.h"
#include "xml/PmuXML.h"

#include <array>
#include <cstddef>

#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#define TYPE_DERIVED ~0U

// from driver/perf/arm_spe_pmu.c
// alternatively we could read them from /sys/devices/arm_spe_0/format/*
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

// An improved version would mask out old value be we assume 0
#define SET_SPE_CFG(cfg, value) SPE_##cfg##_CFG |= static_cast<uint64_t>(value) << SPE_##cfg##_LO

static constexpr uint64_t armv7AndLaterClockCyclesEvent = 0x11;
static constexpr uint64_t armv7PmuDriverCycleCounterPseudoEvent = 0xFF;

class PerfCounter : public DriverCounter {
public:
    static constexpr uint64_t noConfigId2 = ~0ULL;

    PerfCounter(DriverCounter * next,
                const PerfEventGroupIdentifier & groupIdentifier,
                const char * name,
                const IPerfGroups::Attr & attr,
                bool usesAux,
                uint64_t config_id2 = noConfigId2,
                bool fixUpClockCyclesEvent = false)
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
                bool fixUpClockCyclesEvent = false)
        : PerfCounter(next, groupIdentifier, name, IPerfGroups::Attr {}, false, config_id2, fixUpClockCyclesEvent)
    {
        attr.type = type;
        attr.config = config;
        attr.periodOrFreq = count;
        attr.sampleType = sampleType;
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

    inline void setSampleType(uint64_t sampleType) { attr.sampleType = sampleType; }

private:
    const PerfEventGroupIdentifier eventGroupIdentifier;
    IPerfGroups::Attr attr;
    const uint64_t mConfigId2;
    bool mFixUpClockCyclesEvent;
    bool mUsesAux;
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

static long long _getTracepointId(const TraceFsConstants & traceFsConstants, const char * counter, const char * name)
{
    long long result = getTracepointId(traceFsConstants, name);
    if (result <= 0) {
        LOG_SETUP("%s is disabled\n%s was not found", counter, getTracepointPath(traceFsConstants, name, "id").c_str());
    }
    return result;
}

static std::int64_t _getTracepointId(const TraceFsConstants & traceFsConstants, const char * name)
{
    auto result = getTracepointId(traceFsConstants, name);
    if (result <= 0) {
        LOG_SETUP("%s is disabled\n%s was not found", name, getTracepointPath(traceFsConstants, name, "id").c_str());
    }
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
      mDisableKernelAnnotations(disableKernelAnnotations)
{
    static constexpr std::size_t buffer_size = 64;

    // add CPU PMUs
    for (const auto & perfCpu : mConfig.cpus) {

        if ((perfCpu.pmu_type != PERF_TYPE_RAW) && (perfCpu.pmu_type != PERF_TYPE_HARDWARE)) {
            LOG_DEBUG("Adding cpu counters for %s with type %i", perfCpu.gator_cpu.getCoreName(), perfCpu.pmu_type);
        }
        else if ((perfCpu.gator_cpu.getCpuIds().size() > 1)
                 || (!perfCpu.gator_cpu.hasCpuId(PerfDriverConfiguration::UNKNOWN_CPUID))) {
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
        id = _getTracepointId(traceFsConstants, "Interrupts: SoftIRQ", "irq/softirq_exit");
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

        id = _getTracepointId(traceFsConstants, "Interrupts: IRQ", "irq/irq_handler_exit");
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

        id = _getTracepointId(traceFsConstants, "Scheduler: Switch", SCHED_SWITCH);
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
            id = _getTracepointId(traceFsConstants, "Clock: Frequency", CPU_FREQUENCY);
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
        setCounters(new PerfCounter(getCounters(),
                                    PerfEventGroupIdentifier(),
                                    "Linux_cpu_wait_contention",
                                    TYPE_DERIVED,
                                    -1,
                                    0,
                                    0));
        for (const auto & perfCpu : mConfig.cpus) {
            lib::printf_str_t<buffer_size> buf {"%s_system", perfCpu.gator_cpu.getId()};
            setCounters(new PerfCounter(getCounters(),
                                        PerfEventGroupIdentifier(perfCpu.gator_cpu),
                                        buf,
                                        TYPE_DERIVED,
                                        -1,
                                        0,
                                        0));
            buf.printf("%s_user", perfCpu.gator_cpu.getId());
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
        setCounters(
            new PerfCounter(getCounters(), PerfEventGroupIdentifier(mConfig.cpuNumberToSpeType), speId, attr, true));
    }
}

void PerfDriver::addUncoreCounters(const PerfUncore & perfUncore)
{
    const UncorePmu & pmu = perfUncore.uncore_pmu;
    const int type = perfUncore.pmu_type;

    if (pmu.getHasCyclesCounter()) {
        lib::dyn_printf_str_t name {"%s_ccnt", pmu.getId()};
        setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(pmu), name, type, -1, PERF_SAMPLE_READ, 0));
    }

    for (int j = 0; j < pmu.getPmncCounters(); ++j) {
        lib::dyn_printf_str_t name {"%s_cnt%d", pmu.getId(), j};
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

        long long id = _getTracepointId(traceFsConstants, counter, tracepoint);
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

    bool isSystemWide = getConfig().is_system_wide;
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
        setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), name, attr, false, configId2, false));
    };
    auto addCounter = [addCounterWithConfigId2](const char * name, uint64_t id) {
        addCounterWithConfigId2(name, id, PerfCounter::noConfigId2);
    };

    if (false) { /* These are disabled because they never generate events */
        static const char * const MALI_MIDGARD_PM_STATUS_EVENTS[] = {"PM_SHADER_0",
                                                                     "PM_SHADER_1",
                                                                     "PM_SHADER_2",
                                                                     "PM_SHADER_3",
                                                                     "PM_SHADER_4",
                                                                     "PM_SHADER_5",
                                                                     "PM_SHADER_6",
                                                                     "PM_SHADER_7",
                                                                     "PM_TILER_0",
                                                                     "PM_L2_0",
                                                                     "PM_L2_1"};

        id = _getTracepointId(traceFsConstants, "Mali: PM Status", "mali/mali_pm_status");
        if (id >= 0) {
            for (const auto * i : MALI_MIDGARD_PM_STATUS_EVENTS) {
                lib::printf_str_t<buffer_size> buf {"ARM_Mali-%s_%s", maliFamilyName, i};
                addCounter(buf, id);
                mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), "mali/mali_pm_status");
            }
        }
    }

    id = _getTracepointId(traceFsConstants, MALI_MMU_IN_USE, MALI_TRC_PNT_PATH[MALI_MMU_IN_USE]);
    if (id >= 0) {
        const int id2 = _getTracepointId(traceFsConstants, MALI_PM_STATUS, MALI_TRC_PNT_PATH[MALI_PM_STATUS]);
        for (const auto * i : MALI_MIDGARD_AS_IN_USE_RELEASED) {
            lib::printf_str_t<buffer_size> buf {"ARM_Mali-%s_%s", maliFamilyName, i};
            addCounterWithConfigId2(buf, id, id2);
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), MALI_TRC_PNT_PATH[MALI_MMU_IN_USE]);
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), MALI_TRC_PNT_PATH[MALI_PM_STATUS]);
        }
    }

    id = _getTracepointId(traceFsConstants, MALI_MMU_PAGE_FAULT, MALI_TRC_PNT_PATH[MALI_MMU_PAGE_FAULT]);
    if (id >= 0) {
        for (const auto * i : MALI_MIDGARD_PAGE_FAULT_INSERT_PAGES) {
            lib::printf_str_t<buffer_size> buf {"ARM_Mali-%s_%s", maliFamilyName, i};
            addCounter(buf, id);
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), MALI_TRC_PNT_PATH[MALI_MMU_PAGE_FAULT]);
        }
    }

    id = _getTracepointId(traceFsConstants, MALI_MMU_TOTAL_ALLOC, MALI_TRC_PNT_PATH[MALI_MMU_TOTAL_ALLOC]);
    if (id >= 0) {
        lib::printf_str_t<buffer_size> buf {"ARM_Mali-%s_%s", maliFamilyName, MALI_MIDGARD_TOTAL_ALLOC_PAGES};
        addCounter(buf, id);
        mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), MALI_TRC_PNT_PATH[MALI_MMU_TOTAL_ALLOC]);
    }

    // for activity counters
    id = _getTracepointId(traceFsConstants, MALI_JOB_SLOT, MALI_TRC_PNT_PATH[MALI_JOB_SLOT]);
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

std::optional<std::uint64_t> PerfDriver::summary(ISummaryConsumer & consumer,
                                                 const std::function<uint64_t()> & getMonotonicTime)
{
    auto monotonic_started = getMonotonicTime();
    auto state = agents::perf::create_perf_driver_summary_state(mConfig.config, monotonic_started);
    if (!state) {
        return {};
    }

    consumer.summary(state->clock_realtime,
                     state->clock_boottime,
                     state->clock_monotonic_raw,
                     state->uname.c_str(),
                     state->page_size,
                     state->nosync,
                     state->additional_attributes);

    for (size_t i = 0; i < mCpuInfo.getCpuIds().size(); ++i) {
        coreName(consumer, i);
    }

    consumer.flush();

    return {state->clock_monotonic_raw};
}

void PerfDriver::coreName(ISummaryConsumer & consumer, const int cpu)
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
        consumer.coreName(cpu, cpuId, gatorCpu->getCoreName());
    }
    else {
        lib::printf_str_t<32> buf {"Unknown (0x%.3x)", cpuId};
        consumer.coreName(cpu, cpuId, buf);
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

std::optional<CapturedSpe> PerfDriver::setupSpe(int sampleRate, const SpeConfiguration & spe)
{
    for (auto * counter = static_cast<PerfCounter *>(getCounters()); counter != nullptr;
         counter = static_cast<PerfCounter *>(counter->getNext())) {
        if (spe.id == counter->getName()) {
            uint64_t config = 0;
            uint64_t config1 = 0;
            uint64_t config2 = 0;

            SET_SPE_CFG(event_filter, spe.event_filter_mask);
            LOG_DEBUG("Set Spe Event filter mask : 0x%jx\n ", spe.event_filter_mask);
            SET_SPE_CFG(min_latency, spe.min_latency);
            LOG_DEBUG("Set Spe Event min latency : %d\n ", spe.min_latency);
            size_t branchCount = spe.ops.count(SpeOps::BRANCH);
            SET_SPE_CFG(branch_filter, branchCount);
            LOG_DEBUG("Set Spe branch ops count : %zu\n ", branchCount);
            size_t loadCount = spe.ops.count(SpeOps::LOAD);
            SET_SPE_CFG(load_filter, loadCount);
            LOG_DEBUG("Set Spe load ops count : %zu\n ", loadCount);
            size_t storeCount = spe.ops.count(SpeOps::STORE);
            SET_SPE_CFG(store_filter, storeCount);
            LOG_DEBUG("Set Spe store ops count : %zu\n ", storeCount);

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

            if (sampleRate < 0) {
                LOG_DEBUG("SPE: Using default sample rate");
                counter->setCount(100000 /* approx 10KHz sample rate at 1GHz CPU clock */);
            }
            else {
                LOG_DEBUG("SPE: Using user supplied sample rate %d", sampleRate);
                counter->setCount(sampleRate);
            }
            counter->setEnabled(true);

            return {{spe.id, counter->getKey()}};
        }
    }

    return {};
}

bool PerfDriver::enable(IPerfGroups & group, attr_to_key_mapping_tracker_t & mapping_tracker) const
{
    const uint64_t id = getConfig().can_access_tracepoints
                          ? _getTracepointId(traceFsConstants, "Mali: Job slot events", "mali/mali_job_slots_event")
                          : 0 /* never used */;
    bool sentMaliJobSlotEvents = false;

    for (const PerfCpu & cluster : mConfig.cpus) {
        PerfEventGroupIdentifier clusterGroupIdentifier(cluster.gator_cpu);
        group.addGroupLeader(mapping_tracker, clusterGroupIdentifier);
    }
    if (!mDisableKernelAnnotations) {
        std::int64_t id;
        // enable GATOR TRACEPOINTS
        id = _getTracepointId(traceFsConstants, "gator counter", GATOR_COUNTER);
        if (id >= 0) {
            if (!enableGatorTracePoint(group, mapping_tracker, id)) {
                return false;
            }
        }
        id = _getTracepointId(traceFsConstants, "gator bookmark", GATOR_BOOKMARK);
        if (id >= 0) {
            if (!enableGatorTracePoint(group, mapping_tracker, id)) {
                return false;
            }
        }
        id = _getTracepointId(traceFsConstants, "gator text", GATOR_TEXT);
        if (id >= 0) {
            if (!enableGatorTracePoint(group, mapping_tracker, id)) {
                return false;
            }
        }
    }

    for (auto * counter = static_cast<PerfCounter *>(getCounters()); counter != nullptr;
         counter = static_cast<PerfCounter *>(counter->getNext())) {
        if (counter->isEnabled() && (counter->getAttr().type != TYPE_DERIVED)) {
            // do not sent mali_job_slots_event tracepoint multiple times; just send it once and let the processing on the host side
            // deal with multiple counters that are generated from it
            const bool isMaliJobSlotEvents = getConfig().can_access_tracepoints
                                          && (counter->getAttr().type == PERF_TYPE_TRACEPOINT)
                                          && (counter->getAttr().config == id);
            const bool skip = (isMaliJobSlotEvents && sentMaliJobSlotEvents);

            sentMaliJobSlotEvents |= isMaliJobSlotEvents;

            if (!skip) {
                if (group.add(mapping_tracker,
                              counter->getPerfEventGroupIdentifier(),
                              counter->getKey(),
                              counter->getAttr(),
                              counter->usesAux())) {
                    if (counter->hasConfigId2()) {
                        if (!group.add(mapping_tracker,
                                       counter->getPerfEventGroupIdentifier(),
                                       counter->getKey() | 0x40000000,
                                       counter->getAttr2(),
                                       counter->usesAux())) {
                            LOG_DEBUG("PerfGroups::add (2nd) failed");
                            return false;
                        }
                    }
                }
                else {
                    LOG_DEBUG("PerfGroups::add failed");
                    return false;
                }
            }
        }
    }

    return true;
}

bool PerfDriver::enableGatorTracePoint(IPerfGroups & group,
                                       attr_to_key_mapping_tracker_t & mapping_tracker,
                                       long long id) const
{
    IPerfGroups::Attr attr;
    attr.type = PERF_TYPE_TRACEPOINT;
    attr.config = id;
    attr.periodOrFreq = 1;
    attr.sampleType = PERF_SAMPLE_RAW;
    const auto key = getEventKey();
    return group.add(mapping_tracker, PerfEventGroupIdentifier(), key, attr, false);
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
    auto id = _getTracepointId(traceFsConstants, name);
    if ((id >= 0) && (!readTracepointFormat(attrsConsumer, traceFsConstants, name))) {
        return false;
    }
    return true;
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

int PerfDriver::writeCounters(mxml_node_t * root) const
{
    int count = SimpleDriver::writeCounters(root);
    for (const auto & perfCpu : mConfig.cpus) {
        const char * const speName = perfCpu.gator_cpu.getSpeName();
        if (speName != nullptr) {
            mxml_node_t * node = mxmlNewElement(root, "spe");
            mxmlElementSetAttr(node, "id", speName);
            ++count;
        }
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
            use_cpuinfo = static_cast<CPUFreqDriver *>(counter)->isUseCpuInfoPath();
            break;
        }
        result.emplace_back(agents::perf::perf_capture_configuration_t::cpu_freq_properties_t {key, use_cpuinfo});
    }
    return result;
}
