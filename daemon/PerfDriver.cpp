/**
 * Copyright (C) ARM Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "PerfDriver.h"

#include <dirent.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "Buffer.h"
#include "Config.h"
#include "ConfigurationXML.h"
#include "Counter.h"
#include "DriverSource.h"
#include "DynBuf.h"
#include "Logging.h"
#include "PerfGroup.h"
#include "Proc.h"
#include "SessionData.h"
#include "lib/Assert.h"
#include "lib/Time.h"

#define PERF_DEVICES "/sys/bus/event_source/devices"

#define TYPE_DERIVED ~0U

static GatorCpu gatorCpuOther("Other", "Other", NULL, 0xfffff, 6);

class PerfCounter : public DriverCounter
{
public:
    PerfCounter(DriverCounter *next, const char *name, uint32_t type, uint64_t config, uint64_t sampleType,
                uint64_t flags, const GatorCpu * const cluster, const int count)
            : DriverCounter(next, name),
              mType(type),
              mConfig(config),
              mConfigId2(~0ull),
              mSampleType(sampleType),
              mFlags(flags),
              mCluster(cluster),
              mCount(count)
    {
    }

    PerfCounter(DriverCounter *next, const char *name, uint32_t type, uint64_t config, uint64_t sampleType,
                uint64_t flags, const GatorCpu * const cluster, const int count, uint64_t config_id2)
            : DriverCounter(next, name),
              mType(type),
              mConfig(config),
              mConfigId2(config_id2),
              mSampleType(sampleType),
              mFlags(flags),
              mCluster(cluster),
              mCount(count)
    {
    }

    ~PerfCounter()
    {
    }

    uint32_t getType() const
    {
        return mType;
    }
    int getCount() const
    {
        return mCount;
    }
    void setCount(const int count)
    {
        mCount = count;
    }
    uint64_t getConfig() const
    {
        return mConfig;
    }
    void setConfig(const uint64_t config)
    {
        mConfig = config;
    }
    uint64_t getSampleType() const
    {
        return mSampleType;
    }
    void setSampleType(uint64_t sampleType)
    {
        mSampleType = sampleType;
    }
    uint64_t getFlags() const
    {
        return mFlags;
    }
    const GatorCpu *getCluster() const
    {
        return mCluster;
    }

    virtual void read(Buffer * const, const int)
    {
    }

    bool hasConfigId2() const
    {
        return mConfigId2 != ~0ull;
    }

    uint64_t getConfigId2() const
    {
        return mConfigId2;
    }

private:
    const uint32_t mType;
    uint64_t mConfig;
    uint64_t mConfigId2;
    uint64_t mSampleType;
    const uint64_t mFlags;
    const GatorCpu * const mCluster;
    int mCount;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(PerfCounter);
};

class CPUFreqDriver : public PerfCounter
{
public:
    CPUFreqDriver(DriverCounter *next, const char *name, uint64_t id, const GatorCpu * const cluster)
            : PerfCounter(next, name, PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_RAW, PERF_GROUP_LEADER | PERF_GROUP_PER_CPU,
                          cluster, 1)
    {
    }

    void read(Buffer * const buffer, const int cpu)
    {
        if (gSessionData.mSharedData->mClusters[gSessionData.mSharedData->mClusterIds[cpu]] != getCluster()) {
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
          legacySupport(false),
          clockidSupport(false)
{
}

std::unique_ptr<PerfDriver::PerfDriverConfiguration> PerfDriver::detect()
{
    // Check the kernel version
    int release[3];
    if (!getLinuxVersion(release)) {
        logg.logMessage("getLinuxVersion failed");
        return nullptr;
    }

    const int kernelVersion = KERNEL_VERSION(release[0], release[1], release[2]);
    if (kernelVersion < KERNEL_VERSION(3, 4, 0)) {
        logg.logSetup("Unsupported kernel version\nPlease upgrade to 3.4 or later");
        return nullptr;
    }

    if (access(EVENTS_PATH, R_OK) != 0) {
        logg.logSetup(EVENTS_PATH " does not exist\nIs CONFIG_TRACING and CONFIG_CONTEXT_SWITCH_TRACER enabled?");
        return nullptr;
    }

    // Add supported PMUs
    std::unique_ptr<DIR, int (*)(DIR*)> dir { opendir(PERF_DEVICES), closedir };
    if (dir == NULL) {
        logg.logMessage("opendir failed");
        return nullptr;
    }

    // create the configuration object, from this point on perf is supported
    std::unique_ptr<PerfDriverConfiguration> configuration { new PerfDriverConfiguration() };

    configuration->legacySupport = (kernelVersion < KERNEL_VERSION(3, 12, 0));
    configuration->clockidSupport = (kernelVersion >= KERNEL_VERSION(4, 2, 0));

    // detect the PMUs
    struct dirent * dirent;
    while ((dirent = readdir(dir.get())) != NULL) {
        logg.logMessage("perf pmu: %s", dirent->d_name);
        GatorCpu *gatorCpu = GatorCpu::find(dirent->d_name);
        if (gatorCpu != NULL) {
            int type;
            char buf[256];
            snprintf(buf, sizeof(buf), PERF_DEVICES "/%s/type", dirent->d_name);
            if (DriverSource::readIntDriver(buf, &type) == 0) {
                gatorCpu->setType(type);
                configuration->foundCpu = true;
                configuration->cpuPmus.emplace_back(gatorCpu);
                continue;
            }
        }

        UncorePmu * uncorePmu = UncorePmu::find(dirent->d_name);
        if (uncorePmu != NULL) {
            int type;
            char buf[256];
            snprintf(buf, sizeof(buf), PERF_DEVICES "/%s/type", dirent->d_name);
            if (DriverSource::readIntDriver(buf, &type) == 0) {
                uncorePmu->setType(type);
                configuration->uncorePmus.emplace_back(uncorePmu);
                continue;
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
        }
    }

    if (!configuration->foundCpu) {
        logCpuNotFound();
#if defined(__arm__) || defined(__aarch64__)
        gatorCpuOther.setType(PERF_TYPE_RAW);
        configuration->cpuPmus.emplace_back(&gatorCpuOther);
#endif
    }

    return configuration;
}

PerfDriver::PerfDriver(const PerfDriverConfiguration & configuration)
        : mTracepoints(nullptr),
          mIsSetup(false),
          mLegacySupport(configuration.legacySupport),
          mClockidSupport(configuration.clockidSupport)
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
        addUncoreCounters(uncorePmu->getCoreName(), uncorePmu->getType(), uncorePmu->getPmncCounters(),
                          uncorePmu->getHasCyclesCounter());
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

    id = getTracepointId("Interrupts: SoftIRQ", "irq/softirq_exit", &printb);
    if (id >= 0) {
        for (int cluster = 0; cluster < gSessionData.mSharedData->mClusterCount; ++cluster) {
            snprintf(buf, sizeof(buf), "%s_softirq", gSessionData.mSharedData->mClusters[cluster]->getPmncName());
            setCounters(
                    new PerfCounter(getCounters(), strdup(buf), PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_READ,
                                    PERF_GROUP_PER_CPU | PERF_GROUP_CPU, gSessionData.mSharedData->mClusters[cluster],
                                    0));
        }
    }

    id = getTracepointId("Interrupts: IRQ", "irq/irq_handler_exit", &printb);
    if (id >= 0) {
        for (int cluster = 0; cluster < gSessionData.mSharedData->mClusterCount; ++cluster) {
            snprintf(buf, sizeof(buf), "%s_irq", gSessionData.mSharedData->mClusters[cluster]->getPmncName());
            setCounters(
                    new PerfCounter(getCounters(), strdup(buf), PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_READ,
                                    PERF_GROUP_PER_CPU | PERF_GROUP_CPU, gSessionData.mSharedData->mClusters[cluster],
                                    0));
        }
    }

    id = getTracepointId("Scheduler: Switch", SCHED_SWITCH, &printb);
    if (id >= 0) {
        for (int cluster = 0; cluster < gSessionData.mSharedData->mClusterCount; ++cluster) {
            snprintf(buf, sizeof(buf), "%s_switch", gSessionData.mSharedData->mClusters[cluster]->getPmncName());
            setCounters(
                    new PerfCounter(getCounters(), strdup(buf), PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_READ,
                                    PERF_GROUP_PER_CPU | PERF_GROUP_CPU, gSessionData.mSharedData->mClusters[cluster],
                                    0));
        }
    }

    id = getTracepointId("Clock: Frequency", CPU_FREQUENCY, &printb);
    if (id >= 0 && access("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq", R_OK) == 0) {
        for (int cluster = 0; cluster < gSessionData.mSharedData->mClusterCount; ++cluster) {
            snprintf(buf, sizeof(buf), "%s_freq", gSessionData.mSharedData->mClusters[cluster]->getPmncName());
            setCounters(
                    new CPUFreqDriver(getCounters(), strdup(buf), id, gSessionData.mSharedData->mClusters[cluster]));
        }
    }

    setCounters(new PerfCounter(getCounters(), strdup("Linux_cpu_wait_contention"), TYPE_DERIVED, -1, 0, 0, NULL, 0));
    for (int cluster = 0; cluster < gSessionData.mSharedData->mClusterCount; ++cluster) {
        snprintf(buf, sizeof(buf), "%s_system", gSessionData.mSharedData->mClusters[cluster]->getPmncName());
        setCounters(new PerfCounter(getCounters(), strdup(buf), TYPE_DERIVED, -1, 0, 0, NULL, 0));
        snprintf(buf, sizeof(buf), "%s_user", gSessionData.mSharedData->mClusters[cluster]->getPmncName());
        setCounters(new PerfCounter(getCounters(), strdup(buf), TYPE_DERIVED, -1, 0, 0, NULL, 0));
    }

    // add
    const char * const maliFamilyName = gSessionData.mMaliHwCntrs.getSupportedDeviceFamilyName();
    if (maliFamilyName != NULL) {
        // add midgard software tracepoints
        addMidgardHwTracepoints(maliFamilyName);
    }

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
            new PerfCounter(getCounters(), name, cpu->getType(), -1, PERF_SAMPLE_READ,
                            PERF_GROUP_PER_CPU | PERF_GROUP_CPU, cpu, 0));

    for (int j = 0; j < cpu->getPmncCounters(); ++j) {
        len = snprintf(NULL, 0, "%s_cnt%d", cpu->getPmncName(), j) + 1;
        name = new char[len];
        snprintf(name, len, "%s_cnt%d", cpu->getPmncName(), j);
        setCounters(
                new PerfCounter(getCounters(), name, cpu->getType(), -1, PERF_SAMPLE_READ,
                                PERF_GROUP_PER_CPU | PERF_GROUP_CPU, cpu, 0));
    }
}

void PerfDriver::addUncoreCounters(const char * const counterName, const int type, const int numCounters,
                                   const bool hasCyclesCounter)
{
    int len;
    char *name;

    if (hasCyclesCounter) {
        len = snprintf(NULL, 0, "%s_ccnt", counterName) + 1;
        name = new char[len];
        snprintf(name, len, "%s_ccnt", counterName);
        setCounters(new PerfCounter(getCounters(), name, type, -1, PERF_SAMPLE_READ, 0, NULL, 0));
    }

    for (int j = 0; j < numCounters; ++j) {
        len = snprintf(NULL, 0, "%s_cnt%d", counterName, j) + 1;
        name = new char[len];
        snprintf(name, len, "%s_cnt%d", counterName, j);
        setCounters(new PerfCounter(getCounters(), name, type, -1, PERF_SAMPLE_READ, 0, NULL, 0));
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
    if (!mIsSetup) {
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
                    new PerfCounter(getCounters(), strdup(counter), PERF_TYPE_TRACEPOINT, id,
                                    arg == NULL ? 0 : PERF_SAMPLE_RAW,
                                    PERF_GROUP_LEADER | PERF_GROUP_PER_CPU | PERF_GROUP_ALL_CLUSTERS, NULL, 1));
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), strdup(tracepoint));
        }
    }
}

#define COUNT_OF(X) (sizeof(X) / sizeof(X[0]))

void PerfDriver::addMidgardHwTracepoints(const char * const maliFamilyName)
{
    static const char * const MALI_MIDGARD_AS_IN_USE_RELEASED[] = { "MMU_AS_0", "MMU_AS_1", "MMU_AS_2", "MMU_AS_3" };

    static const char * const MALI_MIDGARD_PAGE_FAULT_INSERT_PAGES[] = { "MMU_PAGE_FAULT_0", "MMU_PAGE_FAULT_1",
                                                                         "MMU_PAGE_FAULT_2", "MMU_PAGE_FAULT_3" };

    static const char * const MALI_MIDGARD_TOTAL_ALLOC_PAGES = "TOTAL_ALLOC_PAGES";

    static const __u32 MALI_SAMPLE_TYPE = PERF_SAMPLE_RAW;
    static const int MALI_FLAGS = PERF_GROUP_LEADER | PERF_GROUP_CPU | PERF_GROUP_TASK | PERF_GROUP_SAMPLE_ID_ALL
            | PERF_GROUP_ALL_CLUSTERS | PERF_GROUP_PER_CPU;

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
                    new PerfCounter(getCounters(), strdup(buf), PERF_TYPE_TRACEPOINT, id, MALI_SAMPLE_TYPE, MALI_FLAGS,
                                    NULL, 1, id2));
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), strdup("mali/mali_mmu_as_in_use"));
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), strdup("mali/mali_mmu_as_released"));
        }
    }

    id = getTracepointId("Mali: MMU page fault insert pages", "mali/mali_page_fault_insert_pages", &printb);
    if (id >= 0) {
        for (size_t i = 0; i < COUNT_OF(MALI_MIDGARD_PAGE_FAULT_INSERT_PAGES); ++i) {
            snprintf(buf, sizeof(buf), "ARM_Mali-%s_%s", maliFamilyName, MALI_MIDGARD_PAGE_FAULT_INSERT_PAGES[i]);
            setCounters(
                    new PerfCounter(getCounters(), strdup(buf), PERF_TYPE_TRACEPOINT, id, MALI_SAMPLE_TYPE, MALI_FLAGS,
                                    NULL, 1));
            mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), strdup("mali/mali_page_fault_insert_pages"));
        }
    }

    id = getTracepointId("Mali: MMU total alloc pages changed", "mali/mali_total_alloc_pages_change", &printb);
    if (id >= 0) {
        snprintf(buf, sizeof(buf), "ARM_Mali-%s_%s", maliFamilyName, MALI_MIDGARD_TOTAL_ALLOC_PAGES);
        setCounters(
                new PerfCounter(getCounters(), strdup(buf), PERF_TYPE_TRACEPOINT, id, MALI_SAMPLE_TYPE, MALI_FLAGS,
                                NULL, 1));
        mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), strdup("mali/mali_total_alloc_pages_change"));
    }

    // for activity counters
    id = getTracepointId("Mali: Job slot events", "mali/mali_job_slots_event", &printb);
    if (id >= 0) {
        snprintf(buf, sizeof(buf), "ARM_Mali-%s_fragment", maliFamilyName);
        setCounters(new PerfCounter(getCounters(), strdup(buf), PERF_TYPE_TRACEPOINT, id, MALI_SAMPLE_TYPE, MALI_FLAGS,
                                    NULL, 1));
        snprintf(buf, sizeof(buf), "ARM_Mali-%s_vertex", maliFamilyName);
        setCounters(new PerfCounter(getCounters(), strdup(buf), PERF_TYPE_TRACEPOINT, id, MALI_SAMPLE_TYPE, MALI_FLAGS,
                                    NULL, 1));
        snprintf(buf, sizeof(buf), "ARM_Mali-%s_opencl", maliFamilyName);
        setCounters(new PerfCounter(getCounters(), strdup(buf), PERF_TYPE_TRACEPOINT, id, MALI_SAMPLE_TYPE, MALI_FLAGS,
                                    NULL, 1));
        mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), strdup("mali/mali_job_slots_event"));
    }
}

void logCpuNotFound()
{
#if defined(__arm__) || defined(__aarch64__)
    logg.logSetup("CPU is not recognized\nUsing the ARM architected counters");
#else
    logg.logSetup("CPU is not recognized\nOmitting CPU counters");
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

    buffer->summary(currTime, timestamp, monotonicStarted, monotonicStarted, buf, pageSize, getClockidSupport());

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

    // Don't use the config from counters XML if it's not set, ex: software counters
    if (counter.getEvent() != -1) {
        perfCounter->setConfig(counter.getEvent());
    }
    if (counter.getCount() > 0) {
        // EBS
        perfCounter->setCount(counter.getCount());
        // Collect samples
        perfCounter->setSampleType(perfCounter->getSampleType() | PERF_SAMPLE_TID | PERF_SAMPLE_IP);
    }
    perfCounter->setEnabled(true);
    counter.setKey(perfCounter->getKey());
}

bool PerfDriver::enable(const uint64_t currTime, PerfGroup * const group, Buffer * const buffer) const
{
    DynBuf printb;
    const uint64_t id = getTracepointId("Mali: Job slot events", "mali/mali_job_slots_event", &printb);
    bool sentMaliJobSlotEvents = false;

    for (PerfCounter *counter = static_cast<PerfCounter *>(getCounters()); counter != NULL;
            counter = static_cast<PerfCounter *>(counter->getNext()))
    {
        if (counter->isEnabled() && (counter->getType() != TYPE_DERIVED)) {
            // do not sent mali_job_slots_event tracepoint multiple times; just send it once and let the processing on the host side
            // deal with multiple counters that are generated from it
            const bool isMaliJobSlotEvents = (counter->getType() == PERF_TYPE_TRACEPOINT) &&
                                             (counter->getConfig() == id);
            const bool skip = (isMaliJobSlotEvents && sentMaliJobSlotEvents);

            sentMaliJobSlotEvents |= isMaliJobSlotEvents;

            if (!skip) {
                if (group->add(currTime, buffer, counter->getKey(), counter->getType(), counter->getConfig(),
                               counter->getCount(), counter->getSampleType(), counter->getFlags(), counter->getCluster())) {
                    if (counter->hasConfigId2()) {
                        if (!group->add(currTime, buffer, counter->getKey() | 0x40000000, counter->getType(),
                                        counter->getConfigId2(), counter->getCount(), counter->getSampleType(),
                                        counter->getFlags(), counter->getCluster())) {
                            logg.logMessage("PerfGroup::add (2nd) failed");
                            return false;
                        }
                    }
                }
                else {
                    logg.logMessage("PerfGroup::add failed");
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
