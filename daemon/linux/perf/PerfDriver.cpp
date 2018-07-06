/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "linux/perf/PerfDriver.h"

#include <memory>

#include <dirent.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "Buffer.h"
#include "Config.h"
#include "ConfigurationXML.h"
#include "Counter.h"
#include "DriverSource.h"
#include "DynBuf.h"
#include "Logging.h"
#include "linux/perf/PerfGroups.h"
#include "linux/perf/PerfEventGroupIdentifier.h"
#include "Proc.h"
#include "SessionData.h"
#include "lib/Assert.h"
#include "lib/Format.h"
#include "lib/Time.h"
#include "lib/Popen.h"

#define PERF_DEVICES "/sys/bus/event_source/devices"

#define TYPE_DERIVED ~0U

static constexpr uint64_t armv7AndLaterClockCyclesEvent = 0x11;
static constexpr uint64_t armv7PmuDriverCycleCounterPsuedoEvent = 0xFF;

#if defined(__arm__) || defined(__aarch64__)
static GatorCpu gatorCpuOther("Other", "Other", NULL, 0xfffff, 6);
#else
static GatorCpu gatorCpuOther("Other", "Perf_Hardware", NULL, 0xfffff, 6);
#endif

class PerfCounter : public DriverCounter
{
public:
    static constexpr uint64_t noConfigId2 = ~0ull;

    PerfCounter(DriverCounter *next, const PerfEventGroupIdentifier & groupIdentifier, const char *name, uint32_t type,
                uint64_t config, uint64_t sampleType, uint64_t flags, const int count, uint64_t config_id2 = noConfigId2,
                bool fixUpClockCyclesEvent = false)
            : DriverCounter(next, name),
              eventGroupIdentifier(groupIdentifier),
              mType(type),
              mFlags(flags),
              mConfigId2(config_id2),
              mConfig(config),
              mSampleType(sampleType),
              mCount(count),
              mFixUpClockCyclesEvent(fixUpClockCyclesEvent)
    {
    }

    virtual ~PerfCounter()
    {
    }

    virtual void read(Buffer * const, const int)
    {
    }

    inline const PerfEventGroupIdentifier & getPerfEventGroupIdentifier() const
    {
        return eventGroupIdentifier;
    }

    inline uint32_t getType() const
    {
        return mType;
    }

    inline int getCount() const
    {
        return mCount;
    }

    inline uint64_t getConfig() const
    {
        return mConfig;
    }

    inline uint64_t getSampleType() const
    {
        return mSampleType;
    }

    inline uint64_t getFlags() const
    {
        return mFlags;
    }

    inline bool hasConfigId2() const
    {
        return mConfigId2 != noConfigId2;
    }

    inline uint64_t getConfigId2() const
    {
        return mConfigId2;
    }

    inline void setCount(const int count)
    {
        mCount = count;
    }

    inline void setConfig(const uint64_t config)
    {
        // The Armv7 PMU driver in the linux kernel uses a special event number for the cycle counter
        // that is different from the clock cycles event number.
        // https://github.com/torvalds/linux/blob/0adb32858b0bddf4ada5f364a84ed60b196dbcda/arch/arm/kernel/perf_event_v7.c#L1042
        if (mFixUpClockCyclesEvent && config == armv7AndLaterClockCyclesEvent)
            mConfig = armv7PmuDriverCycleCounterPsuedoEvent;
        else
            mConfig = config;
    }

    inline void setSampleType(uint64_t sampleType)
    {
        mSampleType = sampleType;
    }
private:

    const PerfEventGroupIdentifier eventGroupIdentifier;
    const uint32_t mType;
    const uint64_t mFlags;
    const uint64_t mConfigId2;
    uint64_t mConfig;
    uint64_t mSampleType;
    int mCount;
    bool mFixUpClockCyclesEvent;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(PerfCounter);
};

class CPUFreqDriver : public PerfCounter
{
public:
    CPUFreqDriver(DriverCounter *next, const char *name, uint64_t id, const GatorCpu & cluster)
            : PerfCounter(next, PerfEventGroupIdentifier(cluster),
                          name, PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_RAW, 0, 1)
    {
    }

    void read(Buffer * const buffer, const int cpu)
    {
        if (gSessionData.mSharedData->mClusters[gSessionData.mSharedData->mClusterIds[cpu]] != getPerfEventGroupIdentifier().getCluster()) {
            return;
        }

        char buf[64];

        snprintf(buf, sizeof(buf), "/sys/devices/system/cpu/cpu%i/cpufreq/cpuinfo_cur_freq", cpu);
        int64_t freq;
        if (DriverSource::readInt64Driver(buf, &freq) != 0) {
            freq = 0;
        }
        buffer->perfCounter(cpu, getKey(), 1000 * freq);
    }

private:

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(CPUFreqDriver);
};

PerfDriver::PerfDriverConfiguration::PerfDriverConfiguration()
        : cpuPmus(),
          uncorePmus(),
          foundCpu(false),
          config()
{
}

static bool getPerfHarden()
{
    const char * const command[] = { "getprop", "security.perf_harden", nullptr };
    const lib::PopenResult getprop = lib::popen(command);
    if (getprop.pid < 0) {
        logg.logMessage("lib::popen(%s %s) failed: %s. Probably not android", command[0], command[1], strerror(-getprop.pid));
        return false;
    }

    char value = '0';
    read(getprop.out, &value, 1);
    lib::pclose(getprop);
    return value == '1';
}

static void setPerfHarden(bool on)
{
    const char* const command[] = { "setprop", "security.perf_harden", on ? "1" : "0", nullptr };

    const lib::PopenResult setprop = lib::popen(command);
    if (setprop.pid < 0) {
        logg.logError("lib::popen(%s %s %s) failed: %s", command[0], command[1], command[2], strerror(-setprop.pid));
        return;
    }

    const int status = lib::pclose(setprop);
    if (!WIFEXITED(status)) {
        logg.logError("'%s %s %s' exited abnormally", command[0], command[1], command[2]);
        return;
    }

    const int exitCode = WEXITSTATUS(status);
    if (exitCode != 0) {
        logg.logError("'%s %s %s' failed: %d", command[0], command[1], command[2], exitCode);
    }
}

/**
 * @return true if perf harden in now off
 */
static bool disablePerfHarden()
{
    if (!getPerfHarden())
        return true;

    logg.logWarning("disabling property security.perf_harden");

    setPerfHarden(false);

    sleep(1);

    return !getPerfHarden();
}

static bool beginsWith(const char* string, const char* prefix)
{
    return strncmp(string, prefix, strlen(prefix)) == 0;
}

std::unique_ptr<PerfDriver::PerfDriverConfiguration> PerfDriver::detect(bool systemWide)
{
    // Check the kernel version
    int release[3];
    if (!getLinuxVersion(release)) {
        logg.logMessage("getLinuxVersion failed");
        return nullptr;
    }

    struct utsname utsname;
    if (uname(&utsname) != 0) {
        logg.logMessage("uname failed");
        return nullptr;
    }
    const bool hasArmv7PmuDriver = beginsWith(utsname.machine, "arm") && !beginsWith(utsname.machine, "arm64")
            && !beginsWith(utsname.machine, "armv6");

    const int kernelVersion = KERNEL_VERSION(release[0], release[1], release[2]);
    if (kernelVersion < KERNEL_VERSION(3, 4, 0)) {
        logg.logSetup("Unsupported kernel version\nPlease upgrade to 3.4 or later");
        return nullptr;
    }

    const bool isRoot = (geteuid() == 0);

    if (!isRoot && !disablePerfHarden()) {
        logg.logSetup("failed to disable property security.perf_harden\n" //
                "try 'adb shell setprop security.perf_harden 0'");
        return nullptr;
    }

    int perf_event_paranoid;
    if (DriverSource::readIntDriver("/proc/sys/kernel/perf_event_paranoid", &perf_event_paranoid) != 0) {
        if (isRoot) {
            logg.logSetup("perf_event_open not accessible\nIs CONFIG_PERF_EVENTS enabled?");
            return nullptr;
        }
        else {
            logg.logSetup("perf_event_open not accessible\nAssuming high paranoia.");
            perf_event_paranoid = 3;
        }
    }
    else {
        logg.logMessage("perf_event_paranoid: %d", perf_event_paranoid);
    }

    const bool exclude_kernel = (!isRoot) && (perf_event_paranoid > 1);
    const bool allow_system_wide = isRoot || perf_event_paranoid <= 0;

    if (systemWide && !allow_system_wide) {
        logg.logSetup("System wide tracing\nperf_event_paranoid > 0 not supported for system-wide non-root");
        return nullptr;
    }

    const bool can_access_tracepoints = (access(EVENTS_PATH, R_OK) == 0) && (isRoot || perf_event_paranoid == -1);
    if (can_access_tracepoints)
    {
        logg.logMessage("Have access to tracepoints");
    }
    else
    {
        logg.logMessage("Don't have access to tracepoints");
    }

    // Must have tracepoints or perf_event_attr.context_switch for sched switch info
    if (systemWide && (!can_access_tracepoints) && (kernelVersion < KERNEL_VERSION(4, 3, 0))) {
        logg.logSetup(EVENTS_PATH " does not exist\nIs CONFIG_TRACING and CONFIG_CONTEXT_SWITCH_TRACER enabled?");
        return nullptr;
    }

    // Add supported PMUs
    std::unique_ptr<DIR, int (*)(DIR*)> dir { opendir(PERF_DEVICES), closedir };
    if (dir == NULL) {
        logg.logMessage(PERF_DEVICES " opendir failed");
        //return nullptr;
    }

    // create the configuration object, from this point on perf is supported
    std::unique_ptr<PerfDriverConfiguration> configuration { new PerfDriverConfiguration() };

    configuration->config.has_fd_cloexec = (kernelVersion >= KERNEL_VERSION(3, 14, 0));
    configuration->config.has_count_sw_dummy = (kernelVersion >= KERNEL_VERSION(3, 12, 0));
    configuration->config.has_sample_identifier = (kernelVersion >= KERNEL_VERSION(3, 12, 0));
    configuration->config.has_attr_comm_exec = (kernelVersion >= KERNEL_VERSION(3, 16, 0));
    configuration->config.has_attr_mmap2 = (kernelVersion >= KERNEL_VERSION(3, 16, 0));
    configuration->config.has_attr_clockid_support = (kernelVersion >= KERNEL_VERSION(4, 1, 0));
    configuration->config.has_attr_context_switch = (kernelVersion >= KERNEL_VERSION(4, 3, 0));
    configuration->config.has_ioctl_read_id = (kernelVersion >= KERNEL_VERSION(3, 12, 0));
    configuration->config.has_aux_support = (kernelVersion >= KERNEL_VERSION(4, 1, 0));

    configuration->config.is_system_wide = systemWide;
    configuration->config.exclude_kernel = exclude_kernel;
    configuration->config.allow_system_wide = allow_system_wide;
    configuration->config.can_access_tracepoints = can_access_tracepoints;

    configuration->config.has_armv7_pmu_driver = hasArmv7PmuDriver;

    // detect the PMUs
    unsigned numCpuPmusDetectedViaSysFs = 0;
    unsigned numCpuPmusDetectedViaCpuid = 0;
    if (dir != NULL) {
        struct dirent * dirent;
        while ((dirent = readdir(dir.get())) != NULL) {
            logg.logMessage("perf pmu: %s", dirent->d_name);
            GatorCpu *gatorCpu = GatorCpu::find(dirent->d_name);
            if (gatorCpu != NULL) {
                int type;
                const std::string path (lib::Format() << PERF_DEVICES << "/" << dirent->d_name << "/type");
                if (DriverSource::readIntDriver(path.c_str(), &type) == 0) {
                    gatorCpu->setType(type);
                    configuration->foundCpu = true;
                    configuration->cpuPmus.emplace_back(gatorCpu);
                    ++numCpuPmusDetectedViaSysFs;
                    continue;
                }
            }

            UncorePmu * uncorePmu = UncorePmu::find(dirent->d_name);
            if (uncorePmu != NULL) {
                int type;
                const std::string path (lib::Format() << PERF_DEVICES << "/" << dirent->d_name << "/type");
                if (DriverSource::readIntDriver(path.c_str(), &type) == 0) {
                    uncorePmu->setType(type);
                    configuration->uncorePmus.emplace_back(uncorePmu);
                    continue;
                }
            }
        }
    }

    // additionally add any by CPUID
    for (int processor = 0; processor < NR_CPUS; ++processor) {
        GatorCpu *gatorCpu = GatorCpu::find(gSessionData.mSharedData->mCpuIds[processor]);
        if ((gatorCpu != NULL) && (!gatorCpu->isTypeValid())) {
            gatorCpu->setType(PERF_TYPE_RAW);
            configuration->foundCpu = true;
            configuration->cpuPmus.emplace_back(gatorCpu);
            ++numCpuPmusDetectedViaCpuid;
        }
    }

    // force add other
    if (!configuration->foundCpu) {
        logCpuNotFound();
#if defined(__arm__) || defined(__aarch64__)
        gatorCpuOther.setType(PERF_TYPE_RAW);
        configuration->cpuPmus.emplace_back(&gatorCpuOther);
#else
        gatorCpuOther.setType(PERF_TYPE_HARDWARE);
        configuration->cpuPmus.emplace_back(&gatorCpuOther);
#endif
    }

    if ((numCpuPmusDetectedViaSysFs == 0) && (numCpuPmusDetectedViaCpuid > 0) && (dir != NULL)) {
        logg.logSetup("No Perf PMUs detected\n"
                      "Could not detect any Perf PMUs in /sys/bus/event_source/devices/ but the system contains recognised CPUs. "
                      "The system may not support perf hardware counters. Check CONFIG_HW_PERF_EVENTS is set and that the PMU is configured in the target device tree.");
    }

    return configuration;
}

template <typename T>
inline static T & neverNull(T * t)
{
    if (t == nullptr) {
        handleException();
    }
    return *t;
}

PerfDriver::PerfDriver(const PerfDriverConfiguration & configuration)
        : mTracepoints(nullptr),
          mIsSetup(false),
          mConfig(configuration.config)
{
    // add CPU PMUs
    for (GatorCpu * gatorCpu : configuration.cpuPmus) {
        runtime_assert(gatorCpu->isTypeValid(), "GatorCpu type was not valid");

        if (configuration.foundCpu) {
            if (gatorCpu->getType() == PERF_TYPE_RAW) {
                logg.logMessage("Adding cpu counters (based on cpuid) for %s", gatorCpu->getCoreName());
            }
            else {
                logg.logMessage("Adding cpu counters for %s with type %i", gatorCpu->getCoreName(), gatorCpu->getType());
            }
        }
        else {
            logg.logMessage("Adding cpu counters based on default CPU object");
        }

        addCpuCounters(gatorCpu);
    }

    // add uncore PMUs
    for (UncorePmu * uncorePmu : configuration.uncorePmus) {
        runtime_assert(uncorePmu->isTypeValid(), "UncorePmu type was not valid");

        logg.logMessage("Adding uncore counters for %s with type %i", uncorePmu->getCoreName(), uncorePmu->getType());
        addUncoreCounters(uncorePmu);
    }

    if (gSessionData.mSharedData->mClusterCount == 0) {
        gSessionData.mSharedData->mClusters[gSessionData.mSharedData->mClusterCount++] = &gatorCpuOther;
    }
    // Reread cpuinfo so that cluster data is recalculated
    gSessionData.readCpuInfo();

    // Add supported software counters
    long long id;
    DynBuf printb;
    char buf[40];

    if (mConfig.can_access_tracepoints)
    {
        id = getTracepointId("Interrupts: SoftIRQ", "irq/softirq_exit", &printb);
        if (id >= 0) {
            for (int cluster = 0; cluster < gSessionData.mSharedData->mClusterCount; ++cluster) {
                const GatorCpu & clusterObj = neverNull(gSessionData.mSharedData->mClusters[cluster]);
                snprintf(buf, sizeof(buf), "%s_softirq", clusterObj.getPmncName());
                setCounters(
                        new PerfCounter(getCounters(), PerfEventGroupIdentifier(clusterObj),
                                        strdup(buf), PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_READ,
                                        0, 0));
            }
        }

        id = getTracepointId("Interrupts: IRQ", "irq/irq_handler_exit", &printb);
        if (id >= 0) {
            for (int cluster = 0; cluster < gSessionData.mSharedData->mClusterCount; ++cluster) {
                const GatorCpu & clusterObj = neverNull(gSessionData.mSharedData->mClusters[cluster]);
                snprintf(buf, sizeof(buf), "%s_irq", clusterObj.getPmncName());
                setCounters(
                        new PerfCounter(getCounters(), PerfEventGroupIdentifier(clusterObj),
                                        strdup(buf), PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_READ,
                                        0, 0));
            }
        }

        id = getTracepointId("Scheduler: Switch", SCHED_SWITCH, &printb);
        if (id >= 0) {
            for (int cluster = 0; cluster < gSessionData.mSharedData->mClusterCount; ++cluster) {
                const GatorCpu & clusterObj = neverNull(gSessionData.mSharedData->mClusters[cluster]);
                snprintf(buf, sizeof(buf), "%s_switch", clusterObj.getPmncName());
                setCounters(
                        new PerfCounter(getCounters(), PerfEventGroupIdentifier(clusterObj),
                                        strdup(buf), PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_READ,
                                        0, 0));
            }
        }

        id = getTracepointId("Clock: Frequency", CPU_FREQUENCY, &printb);
        if (id >= 0 && access("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq", R_OK) == 0) {
            for (int cluster = 0; cluster < gSessionData.mSharedData->mClusterCount; ++cluster) {
                const GatorCpu & clusterObj = neverNull(gSessionData.mSharedData->mClusters[cluster]);
                snprintf(buf, sizeof(buf), "%s_freq", clusterObj.getPmncName());
                setCounters(
                        new CPUFreqDriver(getCounters(), strdup(buf), id, clusterObj));
            }
        }
    }

    if (mConfig.can_access_tracepoints || mConfig.has_attr_context_switch) {
        setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), strdup("Linux_cpu_wait_contention"), TYPE_DERIVED, -1, 0, 0, 0));
        for (int cluster = 0; cluster < gSessionData.mSharedData->mClusterCount; ++cluster) {
            const GatorCpu & clusterObj = neverNull(gSessionData.mSharedData->mClusters[cluster]);
            snprintf(buf, sizeof(buf), "%s_system", clusterObj.getPmncName());
            setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(clusterObj),
                                        strdup(buf), TYPE_DERIVED, -1, 0, 0, 0));
            snprintf(buf, sizeof(buf), "%s_user", clusterObj.getPmncName());
            setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(clusterObj),
                                        strdup(buf), TYPE_DERIVED, -1, 0, 0, 0));
        }
    }

    // add
    const char * const maliFamilyName = gSessionData.mMaliHwCntrs.getSupportedDeviceFamilyName();
    if (maliFamilyName != NULL) {
        // add midgard software tracepoints
        addMidgardHwTracepoints(maliFamilyName);
    }

    //Adding for performance counters for perf software
    setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), strdup("PERF_COUNT_SW_CPU_CLOCK"), PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK, 0, 0,  0));
    setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), strdup("PERF_COUNT_SW_TASK_CLOCK"), PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK, 0, 0,  0));
    setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), strdup("PERF_COUNT_SW_CONTEXT_SWITCHES"), PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES, 0, 0, 0));
    setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), strdup("PERF_COUNT_SW_CPU_MIGRATIONS"), PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_MIGRATIONS, 0, 0, 0));
    setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), strdup("PERF_COUNT_SW_PAGE_FAULTS"), PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS, 0, 0, 0));
    setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), strdup("PERF_COUNT_SW_PAGE_FAULTS_MAJ"), PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MAJ, 0, 0, 0));
    setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), strdup("PERF_COUNT_SW_PAGE_FAULTS_MIN"), PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MIN, 0, 0, 0));
    setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), strdup("PERF_COUNT_SW_ALIGNMENT_FAULTS"), PERF_TYPE_SOFTWARE, PERF_COUNT_SW_ALIGNMENT_FAULTS, 0, 0, 0));
    setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(), strdup("PERF_COUNT_SW_EMULATION_FAULTS"), PERF_TYPE_SOFTWARE, PERF_COUNT_SW_EMULATION_FAULTS, 0, 0, 0));

    mIsSetup = true;
}

PerfDriver::~PerfDriver()
{
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
        return mTracepoint;
    }

private:
    PerfTracepoint * const mNext;
    const DriverCounter * const mCounter;
    const char * const mTracepoint;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(PerfTracepoint)
    ;
};

void PerfDriver::addCpuCounters(const GatorCpu * const cpu)
{
    int cluster = gSessionData.mSharedData->mClusterCount++;
    if (cluster >= ARRAY_LENGTH(gSessionData.mSharedData->mClusters)) {
        logg.logError("Too many clusters on the target, please increase CLUSTER_COUNT in Config.h");
        handleException();
    }
    gSessionData.mSharedData->mClusters[cluster] = cpu;

    int len = snprintf(NULL, 0, "%s_ccnt", cpu->getPmncName()) + 1;
    char *name = new char[len];
    snprintf(name, len, "%s_ccnt", cpu->getPmncName());
    setCounters(
            new PerfCounter(getCounters(), PerfEventGroupIdentifier(*cpu),
                            name, cpu->getType(), -1, PERF_SAMPLE_READ,
                            0, 0, PerfCounter::noConfigId2, mConfig.has_armv7_pmu_driver));

    for (int j = 0; j < cpu->getPmncCounters(); ++j) {
        len = snprintf(NULL, 0, "%s_cnt%d", cpu->getPmncName(), j) + 1;
        name = new char[len];
        snprintf(name, len, "%s_cnt%d", cpu->getPmncName(), j);
        setCounters(
                new PerfCounter(getCounters(), PerfEventGroupIdentifier(*cpu),
                                name, cpu->getType(), -1, PERF_SAMPLE_READ,
                                0, 0));
    }
}

void PerfDriver::addUncoreCounters(const UncorePmu * pmu)
{
    int len;
    char *name;

    if (pmu->getHasCyclesCounter()) {
        len = snprintf(NULL, 0, "%s_ccnt", pmu->getPmncName()) + 1;
        name = new char[len];
        snprintf(name, len, "%s_ccnt", pmu->getPmncName());
        setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(*pmu),
                                    name, pmu->getType(), -1, PERF_SAMPLE_READ, 0, 0));
    }

    for (int j = 0; j < pmu->getPmncCounters(); ++j) {
        len = snprintf(NULL, 0, "%s_cnt%d", pmu->getPmncName(), j) + 1;
        name = new char[len];
        snprintf(name, len, "%s_cnt%d", pmu->getPmncName(), j);
        setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(*pmu),
                                    name, pmu->getType(), -1, PERF_SAMPLE_READ, 0, 0));
    }
}

long long PerfDriver::getTracepointId(const char * const counter, const char * const name, DynBuf * const printb)
{
    long long result = PerfDriver::getTracepointId(name, printb);
    if (result <= 0) {
        logg.logSetup("%s is disabled\n%s was not found", counter, printb->getBuf());
    }
    return result;
}

void PerfDriver::readEvents(mxml_node_t * const xml)
{
    mxml_node_t *node = xml;
    DynBuf printb;

    // Only for use with perf
    if (!mIsSetup || !mConfig.can_access_tracepoints) {
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

        long long id = getTracepointId(counter, tracepoint, &printb);
        if (id >= 0) {
            logg.logMessage("Using perf for %s", counter);
            setCounters(
                    new PerfCounter(getCounters(), PerfEventGroupIdentifier(),
                                    strdup(counter), PERF_TYPE_TRACEPOINT, id,
                                    arg == NULL ? 0 : PERF_SAMPLE_RAW,
                                    0, 1));
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), strdup(tracepoint));
        }
    }
}

#define COUNT_OF(X) (sizeof(X) / sizeof(X[0]))

void PerfDriver::addMidgardHwTracepoints(const char * const maliFamilyName)
{
    if (!mConfig.can_access_tracepoints)
        return;

    static const char * const MALI_MIDGARD_AS_IN_USE_RELEASED[] = { "MMU_AS_0", "MMU_AS_1", "MMU_AS_2", "MMU_AS_3" };

    static const char * const MALI_MIDGARD_PAGE_FAULT_INSERT_PAGES[] = { "MMU_PAGE_FAULT_0", "MMU_PAGE_FAULT_1",
                                                                         "MMU_PAGE_FAULT_2", "MMU_PAGE_FAULT_3" };

    static const char * const MALI_MIDGARD_TOTAL_ALLOC_PAGES = "TOTAL_ALLOC_PAGES";

    static const uint32_t MALI_SAMPLE_TYPE = PERF_SAMPLE_RAW;
    static const int MALI_FLAGS = PERF_GROUP_TASK | PERF_GROUP_SAMPLE_ID_ALL;

    DynBuf printb;
    long long id;
    char buf[256];

    // add midgard software tracepoints

#if 0 /* These are disabled because they never generate events */
    static const char * const MALI_MIDGARD_PM_STATUS_EVENTS[] = {
        "PM_SHADER_0",
        "PM_SHADER_1",
        "PM_SHADER_2",
        "PM_SHADER_3",
        "PM_SHADER_4",
        "PM_SHADER_5",
        "PM_SHADER_6",
        "PM_SHADER_7",
        "PM_TILER_0",
        "PM_L2_0",
        "PM_L2_1"
    };

    id = getTracepointId("Mali: PM Status", "mali/mali_pm_status", &printb);
    if (id >= 0) {
        for (size_t i = 0; i < COUNT_OF(MALI_MIDGARD_PM_STATUS_EVENTS); ++i) {
            snprintf(buf, sizeof(buf), "ARM_Mali-%s_%s", maliFamilyName, MALI_MIDGARD_PM_STATUS_EVENTS[i]);
            setCounters(new PerfCounter(getCounters(), strdup(buf), PERF_TYPE_TRACEPOINT, id, MALI_SAMPLE_TYPE, MALI_FLAGS, NULL, 1));
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), strdup("mali/mali_pm_status"));
        }
    }
#endif

    id = getTracepointId("Mali: MMU address space in use", "mali/mali_mmu_as_in_use", &printb);
    if (id >= 0) {
        const int id2 = getTracepointId("Mali: PM Status", "mali/mali_mmu_as_released", &printb);
        for (size_t i = 0; i < COUNT_OF(MALI_MIDGARD_PAGE_FAULT_INSERT_PAGES); ++i) {
            snprintf(buf, sizeof(buf), "ARM_Mali-%s_%s", maliFamilyName, MALI_MIDGARD_AS_IN_USE_RELEASED[i]);
            setCounters(
                    new PerfCounter(getCounters(), PerfEventGroupIdentifier(),
                                    strdup(buf), PERF_TYPE_TRACEPOINT, id, MALI_SAMPLE_TYPE, MALI_FLAGS, 1, id2));
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), strdup("mali/mali_mmu_as_in_use"));
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), strdup("mali/mali_mmu_as_released"));
        }
    }

    id = getTracepointId("Mali: MMU page fault insert pages", "mali/mali_page_fault_insert_pages", &printb);
    if (id >= 0) {
        for (size_t i = 0; i < COUNT_OF(MALI_MIDGARD_PAGE_FAULT_INSERT_PAGES); ++i) {
            snprintf(buf, sizeof(buf), "ARM_Mali-%s_%s", maliFamilyName, MALI_MIDGARD_PAGE_FAULT_INSERT_PAGES[i]);
            setCounters(
                    new PerfCounter(getCounters(), PerfEventGroupIdentifier(),
                                    strdup(buf), PERF_TYPE_TRACEPOINT, id, MALI_SAMPLE_TYPE, MALI_FLAGS, 1));
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), strdup("mali/mali_page_fault_insert_pages"));
        }
    }

    id = getTracepointId("Mali: MMU total alloc pages changed", "mali/mali_total_alloc_pages_change", &printb);
    if (id >= 0) {
        snprintf(buf, sizeof(buf), "ARM_Mali-%s_%s", maliFamilyName, MALI_MIDGARD_TOTAL_ALLOC_PAGES);
        setCounters(
                new PerfCounter(getCounters(), PerfEventGroupIdentifier(),
                                strdup(buf), PERF_TYPE_TRACEPOINT, id, MALI_SAMPLE_TYPE, MALI_FLAGS, 1));
        mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), strdup("mali/mali_total_alloc_pages_change"));
    }

    // for activity counters
    id = getTracepointId("Mali: Job slot events", "mali/mali_job_slots_event", &printb);
    if (id >= 0) {
        snprintf(buf, sizeof(buf), "ARM_Mali-%s_fragment", maliFamilyName);
        setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(),
                                    strdup(buf), PERF_TYPE_TRACEPOINT, id, MALI_SAMPLE_TYPE, MALI_FLAGS, 1));
        snprintf(buf, sizeof(buf), "ARM_Mali-%s_vertex", maliFamilyName);
        setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(),
                                    strdup(buf), PERF_TYPE_TRACEPOINT, id, MALI_SAMPLE_TYPE, MALI_FLAGS, 1));
        snprintf(buf, sizeof(buf), "ARM_Mali-%s_opencl", maliFamilyName);
        setCounters(new PerfCounter(getCounters(), PerfEventGroupIdentifier(),
                                    strdup(buf), PERF_TYPE_TRACEPOINT, id, MALI_SAMPLE_TYPE, MALI_FLAGS, 1));
        mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), strdup("mali/mali_job_slots_event"));
    }
}

void logCpuNotFound()
{
#if defined(__arm__) || defined(__aarch64__)
    logg.logSetup("CPU is not recognized\nUsing the Arm architected counters");
#else
    logg.logSetup("CPU is not recognized\nUsing perf hardware counters");
#endif
}

bool PerfDriver::summary(Buffer * const buffer)
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

    const uint64_t monotonicStarted = getTime();
    gSessionData.mMonotonicStarted = monotonicStarted;
    const uint64_t currTime = 0; //getTime() - gSessionData.mMonotonicStarted;

    std::map<const char *, const char *> additionalAttributes;

    additionalAttributes["perf.is_root"] = (geteuid() == 0 ? "1" : "0");
    additionalAttributes["perf.is_system_wide"] = (mConfig.is_system_wide ? "1" : "0");
    additionalAttributes["perf.can_access_tracepoints"] = (mConfig.can_access_tracepoints ? "1" : "0");
    additionalAttributes["perf.has_attr_context_switch"] = (mConfig.has_attr_context_switch ? "1" : "0");

    buffer->summary(currTime, timestamp, monotonicStarted, monotonicStarted, buf, pageSize, mConfig.has_attr_clockid_support, additionalAttributes);

    for (int i = 0; i < gSessionData.mCores; ++i) {
        coreName(currTime, buffer, i);
    }
    buffer->commit(currTime);

    return true;
}

void PerfDriver::coreName(const uint64_t currTime, Buffer * const buffer, const int cpu)
{
    const SharedData * const sharedData = gSessionData.mSharedData;
    // Don't send information on a cpu we know nothing about
    if (sharedData->mCpuIds[cpu] == -1) {
        return;
    }

    GatorCpu *gatorCpu = GatorCpu::find(sharedData->mCpuIds[cpu]);
    if (gatorCpu != NULL && gatorCpu->getCpuid() == sharedData->mCpuIds[cpu]) {
        buffer->coreName(currTime, cpu, sharedData->mCpuIds[cpu], gatorCpu->getCoreName());
    }
    else {
        char buf[32];
        if (sharedData->mCpuIds[cpu] == -1) {
            snprintf(buf, sizeof(buf), "Unknown");
        }
        else {
            snprintf(buf, sizeof(buf), "Unknown (0x%.3x)", sharedData->mCpuIds[cpu]);
        }
        buffer->coreName(currTime, cpu, sharedData->mCpuIds[cpu], buf);
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

    const bool enableCallChain = (gSessionData.mBacktraceDepth > 0);

    // Don't use the config from counters XML if it's not set, ex: software counters
    if (counter.getEvent() != -1) {
        perfCounter->setConfig(counter.getEvent());
    }
    if (counter.getCount() > 0) {
        // EBS
        perfCounter->setCount(counter.getCount());
        // Collect samples
        perfCounter->setSampleType(perfCounter->getSampleType() | PERF_SAMPLE_TID | PERF_SAMPLE_IP
                                   | (enableCallChain ? PERF_SAMPLE_CALLCHAIN : 0));
    }
    perfCounter->setEnabled(true);
    counter.setKey(perfCounter->getKey());
}

bool PerfDriver::enable(const uint64_t currTime, PerfGroups * const group, Buffer * const buffer) const
{
    DynBuf printb;
    const uint64_t id = mConfig.can_access_tracepoints ? getTracepointId("Mali: Job slot events", "mali/mali_job_slots_event", &printb) : 0 /* never used */;
    bool sentMaliJobSlotEvents = false;

    for (int cluster = 0; cluster < gSessionData.mSharedData->mClusterCount; ++cluster) {
        const GatorCpu & clusterObj = neverNull(gSessionData.mSharedData->mClusters[cluster]);
        PerfEventGroupIdentifier clusterGroupIdentifier(clusterObj);
        group->addGroupLeader(currTime, buffer, clusterGroupIdentifier);
    }

    for (PerfCounter *counter = static_cast<PerfCounter *>(getCounters()); counter != NULL;
            counter = static_cast<PerfCounter *>(counter->getNext()))
    {
        if (counter->isEnabled() && (counter->getType() != TYPE_DERIVED)) {
            // do not sent mali_job_slots_event tracepoint multiple times; just send it once and let the processing on the host side
            // deal with multiple counters that are generated from it
            const bool isMaliJobSlotEvents = mConfig.can_access_tracepoints && (counter->getType() == PERF_TYPE_TRACEPOINT) &&
                                             (counter->getConfig() == id);
            const bool skip = (isMaliJobSlotEvents && sentMaliJobSlotEvents);

            sentMaliJobSlotEvents |= isMaliJobSlotEvents;

            if (!skip) {
                if (group->add(currTime, buffer, counter->getPerfEventGroupIdentifier(), counter->getKey(),
                               counter->getType(), counter->getConfig(), counter->getCount(), counter->getSampleType(), counter->getFlags())) {
                    if (counter->hasConfigId2()) {
                        if (!group->add(currTime, buffer, counter->getPerfEventGroupIdentifier(), counter->getKey() | 0x40000000,
                                        counter->getType(), counter->getConfigId2(), counter->getCount(), counter->getSampleType(),
                                        counter->getFlags())) {
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

void PerfDriver::read(Buffer * const buffer, const int cpu)
{
    for (PerfCounter *counter = static_cast<PerfCounter *>(getCounters()); counter != NULL;
            counter = static_cast<PerfCounter *>(counter->getNext())) {
        if (!counter->isEnabled()) {
            continue;
        }
        counter->read(buffer, cpu);
    }
}

bool PerfDriver::sendTracepointFormats(const uint64_t currTime, Buffer * const buffer, DynBuf * const printb,
                                       DynBuf * const b)
{
    if (!readTracepointFormat(currTime, buffer, SCHED_SWITCH, printb, b)
            || !readTracepointFormat(currTime, buffer, CPU_IDLE, printb, b)
            || !readTracepointFormat(currTime, buffer, CPU_FREQUENCY, printb, b) || false) {
        return false;
    }

    for (PerfTracepoint *tracepoint = mTracepoints; tracepoint != NULL; tracepoint = tracepoint->getNext()) {
        if (tracepoint->getCounter()->isEnabled()
                && !readTracepointFormat(currTime, buffer, tracepoint->getTracepoint(), printb, b)) {
            return false;
        }
    }

    return true;
}

long long PerfDriver::getTracepointId(const char * const name, DynBuf * const printb)
{
    if (!printb->printf(EVENTS_PATH "/%s/id", name)) {
        logg.logMessage("DynBuf::printf failed");
        return -1;
    }

    int64_t result;
    if (DriverSource::readInt64Driver(printb->getBuf(), &result) != 0) {
        logg.logMessage("Unable to read tracepoint id for %s", printb->getBuf());
        return -1;
    }

    return result;
}
