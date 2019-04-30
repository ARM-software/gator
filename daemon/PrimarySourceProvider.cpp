/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#include "PrimarySourceProvider.h"
#include "Config.h"
#include "CpuUtils.h"
#include "DiskIODriver.h"
#include "FSDriver.h"
#include "lib/FsEntry.h"
#include "HwmonDriver.h"
#include "ICpuInfo.h"
#include "Logging.h"
#include "MemInfoDriver.h"
#include "NetDriver.h"
#include "PmuXML.h"
#include "SessionData.h"
#include "lib/Utils.h"
#if CONFIG_SUPPORT_GATOR_KO
#   include "KMod.h"
#   include "DriverSource.h"
#endif
#if CONFIG_SUPPORT_PERF
#   include "linux/perf/PerfDriver.h"
#   include "linux/perf/PerfDriverConfiguration.h"
#   include "linux/perf/PerfSource.h"
#endif
#if CONFIG_SUPPORT_PROC_POLLING
#   include "non_root/NonRootDriver.h"
#   include "non_root/NonRootSource.h"
#endif

#include <algorithm>
#include <unistd.h>

static const char CORE_NAME_UNKNOWN[] = "unknown";

extern bool setupFilesystem(const char* module);

namespace
{
    /// array for cpuIds and clusterIds
    class Ids
    {
    public:
        Ids(unsigned int maxCoreNumber)
                : maxCoreNumber(maxCoreNumber)
        {
            for (unsigned int i = 0; i < maxCoreNumber * 2; ++i)
                ids.get()[i] = -1; // Unknown
        }

        lib::Span<int> getCpuIds()
        {
            return {ids.get(), maxCoreNumber};
        }

        lib::Span<const int> getCpuIds() const
        {
            return {ids.get(), maxCoreNumber};
        }

        lib::Span<int> getClusterIds()
        {
            return {ids.get() + maxCoreNumber, maxCoreNumber};
        }

        lib::Span<const int> getClusterIds() const
        {
            return {ids.get() + maxCoreNumber, maxCoreNumber};
        }

    private:
        unsigned int maxCoreNumber;
        shared_memory::unique_ptr<int[]> ids = shared_memory::make_unique<int[]>(maxCoreNumber * 2);
    };


    class CpuInfo : public ICpuInfo
    {
    public:
        CpuInfo(Ids && ids, std::vector<GatorCpu> && clusters, const char * modelName)
                : ids(std::move(ids)),
                  clusters(std::move(clusters)),
                  modelName(modelName)
        {
            std::sort(this->clusters.begin(), this->clusters.end());
            updateClusterIds();
        }

        virtual lib::Span<const int> getCpuIds() const override
        {
            return ids.getCpuIds();
        }

        virtual lib::Span<const GatorCpu> getClusters() const override
        {
            return clusters;
        }

        virtual lib::Span<const int> getClusterIds() const override
        {
            return ids.getClusterIds();
        }

        virtual void updateIds(bool ignoreOffline) override
        {
            cpu_utils::readCpuInfo(ignoreOffline, ids.getCpuIds());
            updateClusterIds();
        }

        virtual const char * getModelName() const override {
            return modelName.c_str();
        }

        void updateClusterIds()
        {
            int lastClusterId = 0;
            for (size_t i = 0; i < ids.getCpuIds().length; ++i) {
                int clusterId = -1;
                for (size_t j = 0; j < clusters.size(); ++j) {
                    const int cpuId = clusters[j].getCpuid();
                    if (ids.getCpuIds()[i] == cpuId) {
                        clusterId = j;
                    }
                }
                if (clusterId == -1) {
                    // No corresponding cluster found for this CPU, most likely this is a big LITTLE system without multi-PMU support
                    // assume it belongs to the last cluster seen
                    ids.getClusterIds()[i] = lastClusterId;
                }
                else {
                    ids.getClusterIds()[i] = clusterId;
                    lastClusterId = clusterId;
                }
            }
        }

    private:
        Ids ids;
        std::vector<GatorCpu> clusters;
        std::string modelName;
    };

#if CONFIG_SUPPORT_GATOR_KO

    /**
     * Primary source that reads from gator.ko
     */
    class GatorKoPrimarySource : public PrimarySourceProvider
    {
    public:

        /**
         *
         * @param module
         * @param pmuXml
         * @param ids will be consumed if sucessful
         * @return
         */
        static std::unique_ptr<PrimarySourceProvider> tryCreate(const char * module, const PmuXML & pmuXml, Ids & ids, const char * modelName)
        {
            std::unique_ptr<PrimarySourceProvider> result;

            if (setupFilesystem(module)) {
                KMod::checkVersion();
                auto && clusters = KMod::writePmuXml(pmuXml);
                CpuInfo cpuInfo {std::move(ids), std::move(clusters), modelName};

                result.reset(new GatorKoPrimarySource(std::move(cpuInfo)));
            }

            return result;
        }

        virtual const char * getCaptureXmlTypeValue() const override
        {
            return "Gator";
        }

        virtual const char * getBacktraceProcessingMode() const override
        {
            return "gator";
        }

        virtual std::int64_t getMonotonicStarted() const override
        {
            std::int64_t monotonicStarted;
            if (lib::readInt64FromFile("/dev/gator/started", monotonicStarted) == -1) {
                logg.logError("Error reading gator driver start time");
                handleException();
            }
            gSessionData.mMonotonicStarted = monotonicStarted;

            return monotonicStarted;
        }

        virtual bool supportsTracepointCapture() const override
        {
            return false;
        }

        virtual bool supportsMaliCapture() const override
        {
            return true;
        }

        virtual bool supportsMaliCaptureSampleRate(int rate) const override
        {
            return rate > 0;
        }

        virtual bool isCapturingMaliCounters() const override
        {
            return driver.isMaliCapture();
        }

        virtual const char * getPrepareFailedMessage() const override
        {
            return "Unable to prepare gator driver for capture";
        }

        virtual const Driver & getPrimaryDriver() const override
        {
            return driver;
        }

        virtual Driver & getPrimaryDriver() override
        {
            return driver;
        }

        virtual const ICpuInfo & getCpuInfo() const override {
            return cpuInfo;
        }

        virtual ICpuInfo & getCpuInfo() override {
            return cpuInfo;
        }

        virtual std::unique_ptr<Source> createPrimarySource(Child & child, sem_t & senderSem,
                                                            sem_t & startProfile, const std::set<int> &, FtraceDriver & ftraceDriver, bool) override
        {
            return std::unique_ptr<Source>(new DriverSource(child, senderSem, startProfile, ftraceDriver));
        }

    private:

        static std::vector<PolledDriver *> createPolledDrivers()
        {
            return std::vector<PolledDriver *> { { new HwmonDriver(), new FSDriver() } };
        }

        GatorKoPrimarySource(CpuInfo && cpuInfo)
                : PrimarySourceProvider(createPolledDrivers()),
                  driver(),
                  cpuInfo(std::move(cpuInfo))
        {
        }

        KMod driver;
        CpuInfo cpuInfo;
    };

#endif /* CONFIG_SUPPORT_GATOR_KO */

#if CONFIG_SUPPORT_PERF
    /**
     * Primary source that reads from Linux perf API
     */
    class PerfPrimarySource : public PrimarySourceProvider
    {
    public:

        /**
         *
         * @param systemWide
         * @param pmuXml consumes this on success
         * @return
         */
        static std::unique_ptr<PrimarySourceProvider> tryCreate(bool systemWide, PmuXML & pmuXml, const char * maliFamilyName, Ids & ids, const char * modelName)
        {
            std::unique_ptr<PerfDriverConfiguration> configuration = PerfDriverConfiguration::detect(systemWide,
                                                                                                     ids.getCpuIds(),
                                                                                                     pmuXml);
            if (configuration != nullptr) {
                std::vector<GatorCpu> clusters;
                for (const auto & perfCpu : configuration->cpus) {
                    clusters.push_back(perfCpu.gator_cpu);
                }
                CpuInfo cpuInfo { std::move(ids), std::move(clusters), modelName };
                return std::unique_ptr<PrimarySourceProvider> { new PerfPrimarySource(std::move(*configuration),
                                                                                      std::move(pmuXml), maliFamilyName,
                                                                                      std::move(cpuInfo)) };
            }

            return nullptr;
        }

        virtual const char * getCaptureXmlTypeValue() const override
        {
            return "Perf";
        }

        virtual const char * getBacktraceProcessingMode() const override
        {
            return "perf";
        }

        virtual std::int64_t getMonotonicStarted() const override
        {
            return gSessionData.mMonotonicStarted;
        }

        virtual bool supportsTracepointCapture() const override
        {
            return true;
        }

        virtual bool supportsMaliCapture() const override
        {
            return false;
        }

        virtual bool supportsMaliCaptureSampleRate(int) const override
        {
            return false;
        }

        virtual bool isCapturingMaliCounters() const override
        {
            return false;
        }

        virtual const char * getPrepareFailedMessage() const override
        {
            return "Unable to communicate with the perf API, please ensure that CONFIG_TRACING and CONFIG_CONTEXT_SWITCH_TRACER are enabled. Please refer to streamline/gator/README.md for more information.";
        }

        virtual const Driver & getPrimaryDriver() const override
        {
            return driver;
        }

        virtual Driver & getPrimaryDriver() override
        {
            return driver;
        }

        virtual const ICpuInfo & getCpuInfo() const override {
            return cpuInfo;
        }

        virtual ICpuInfo & getCpuInfo() override {
            return cpuInfo;
        }

        virtual std::unique_ptr<Source> createPrimarySource(Child & child, sem_t & senderSem,
                                                            sem_t & startProfile, const std::set<int> & appTids, FtraceDriver & ftraceDriver, bool enableOnCommandExec) override
        {
            return std::unique_ptr<Source>(new PerfSource(driver, child, senderSem, startProfile, appTids, ftraceDriver, enableOnCommandExec, cpuInfo));
        }

    private:

        static std::vector<PolledDriver *> createPolledDrivers()
        {
            return std::vector<PolledDriver *> { { new HwmonDriver(), new FSDriver(), new DiskIODriver(),
                                                   new MemInfoDriver(), new NetDriver() } };
        }

        PerfPrimarySource(PerfDriverConfiguration && configuration, PmuXML && pmuXml, const char * maliFamilyName, CpuInfo && cpuInfo)
                : PrimarySourceProvider(createPolledDrivers()),
                  cpuInfo(std::move(cpuInfo)),
                  driver(std::move(configuration), std::move(pmuXml), maliFamilyName, this->cpuInfo)
        {
        }

        CpuInfo cpuInfo;
        PerfDriver driver;
    };
#endif /* CONFIG_SUPPORT_PERF */

#if CONFIG_SUPPORT_PROC_POLLING
    /**
     * Primary source that reads from top-like information from /proc and works in non-root environment
     */
    class NonRootPrimarySource : public PrimarySourceProvider
    {
    public:

        static std::unique_ptr<PrimarySourceProvider> tryCreate(PmuXML && pmuXml, Ids & ids, const char * modelName)
        {
            // detect clusters so we can generate activity events
            std::set<int> added;
            std::vector<GatorCpu> clusters;
            for (int cpuId : ids.getClusterIds()) {
                if (cpuId >= 0 && added.count(cpuId) != 0) {
                    const GatorCpu *gatorCpu = pmuXml.findCpuById(cpuId);
                    if (gatorCpu != nullptr) {
                        // create the cluster
                        added.insert(cpuId);
                        clusters.push_back(*gatorCpu);
                    }
                }
            }

            if (clusters.empty()) {
#if defined(__aarch64__)
                clusters.emplace_back("Other", "Other", nullptr, nullptr, 0xfffff, 6, true);
#elif defined(__arm__)
                clusters.emplace_back("Other", "Other", nullptr, nullptr, 0xfffff, 6, false);
#else
                clusters.emplace_back("Other", "Perf_Hardware", nullptr, nullptr, 0xfffff, 6, false);
#endif
            }

            return std::unique_ptr<PrimarySourceProvider>(new NonRootPrimarySource(std::move(pmuXml), {std::move(ids), std::move(clusters), modelName}));
        }

        virtual const char * getCaptureXmlTypeValue() const override
        {
            // Sends data in gator format
            return "Gator";
        }

        virtual const char * getBacktraceProcessingMode() const override
        {
            return "none";
        }

        virtual std::int64_t getMonotonicStarted() const override
        {
            return gSessionData.mMonotonicStarted;
        }

        virtual bool supportsTracepointCapture() const override
        {
            return true;
        }

        virtual bool supportsMaliCapture() const override
        {
            return false;
        }

        virtual bool supportsMaliCaptureSampleRate(int) const override
        {
            return false;
        }

        virtual bool isCapturingMaliCounters() const override
        {
            return false;
        }

        virtual const char * getPrepareFailedMessage() const override
        {
            return "Could not initialize non-root data capture";
        }

        virtual const Driver & getPrimaryDriver() const override
        {
            return driver;
        }

        virtual Driver & getPrimaryDriver() override
        {
            return driver;
        }

        virtual const ICpuInfo & getCpuInfo() const override {
            return cpuInfo;
        }

        virtual ICpuInfo & getCpuInfo() override {
            return cpuInfo;
        }

        virtual std::unique_ptr<Source> createPrimarySource(Child & child, sem_t & senderSem,
                                                            sem_t & startProfile, const std::set<int> &, FtraceDriver &, bool) override
        {
            return std::unique_ptr<Source>(new non_root::NonRootSource(driver, child, senderSem, startProfile, cpuInfo));
        }

    private:

        static std::vector<PolledDriver *> createPolledDrivers()
        {
            return std::vector<PolledDriver *> { { new HwmonDriver(), new FSDriver(), new DiskIODriver(),
                                                   new MemInfoDriver(), new NetDriver() } };
        }

        NonRootPrimarySource(PmuXML && pmuXml, CpuInfo && cpuInfo)
                : PrimarySourceProvider(createPolledDrivers()),
                  cpuInfo(std::move(cpuInfo)),
                  driver(std::move(pmuXml), this->cpuInfo.getClusters())
        {
        }

        CpuInfo cpuInfo;
        non_root::NonRootDriver driver;
    };
#endif /* CONFIG_SUPPORT_PROC_POLLING */
}

PrimarySourceProvider::PrimarySourceProvider(const std::vector<PolledDriver *> & polledDrivers_)
        : polledDrivers(polledDrivers_)
{
}

PrimarySourceProvider::~PrimarySourceProvider()
{
    for (PolledDriver * polledDriver : polledDrivers) {
        delete polledDriver;
    }
}

const std::vector<PolledDriver *> & PrimarySourceProvider::getAdditionalPolledDrivers() const
{
    return polledDrivers;
}

std::unique_ptr<PrimarySourceProvider> PrimarySourceProvider::detect(const char * module, bool systemWide, PmuXML && pmuXml, const char * maliFamilyName)
{

    Ids ids {cpu_utils::getMaxCoreNum()};
    const std::string modelName = lib::FsEntry::create("/proc/device-tree/model").readFileContents();
    const std::string hardwareName = cpu_utils::readCpuInfo(false, ids.getCpuIds());
    const char * modelNameToUse = !modelName.empty() ? modelName.c_str() : !hardwareName.empty() ? hardwareName.c_str() : CORE_NAME_UNKNOWN;
    std::unique_ptr<PrimarySourceProvider> result;

    // Verify root permissions
    const bool isRoot = (geteuid() == 0);

    logg.logMessage("Determining primary source");

    // try gator.ko
#if CONFIG_SUPPORT_GATOR_KO
    if (isRoot && systemWide) {
        logg.logMessage("Trying gator.ko...");
        result = GatorKoPrimarySource::tryCreate(module, pmuXml, ids, modelNameToUse);
        if (result != nullptr) {
            logg.logMessage("...Success");
            logg.logSetup("Profiling Source\nUsing gator.ko for primary data source");
            return result;
        }
        else
            logg.logMessage("...Unable to set up gator.ko");
    }
#endif /* CONFIG_SUPPORT_GATOR_KO */

    // try perf
#if CONFIG_SUPPORT_PERF
    if (isRoot)
        logg.logMessage("Trying perf API as root...");
    else
        logg.logMessage("Trying perf API as non-root...");

    result = PerfPrimarySource::tryCreate(systemWide, pmuXml, maliFamilyName, ids, modelNameToUse);
    if (result != nullptr) {
        logg.logMessage("...Success");
        logg.logSetup("Profiling Source\nUsing perf API for primary data source");
        return result;
    }
    else
        logg.logMessage("...Perf API is not available to non-root users in system-wide mode.\n"
                "To use it make sure '/proc/sys/kernel/perf_event_paranoid' is set to -1,\n"
                "and '/sys/kernel/debug' and '/sys/kernel/debug/tracing' are mounted and accessible as this user.\n\n"
                "Try (as root):\n"
                " - mount -o remount,mode=755 /sys/kernel/debug\n"
                " - mount -o remount,mode=755 /sys/kernel/debug/tracing\n"
                " - echo -1 > /proc/sys/kernel/perf_event_paranoid");
#endif /* CONFIG_SUPPORT_PERF */

    // fall back to non-root mode
#if CONFIG_SUPPORT_PROC_POLLING
    if (isRoot)
        logg.logMessage("Trying proc counters as root...");
    else
        logg.logMessage("Trying proc counters as non-root; limited system profiling information available...");

    result = NonRootPrimarySource::tryCreate(std::move(pmuXml), ids, modelNameToUse);
    if (result != nullptr) {
        logg.logMessage("...Success");
        logg.logSetup("Profiling Source\nUsing proc polling for primary data source");
        return result;
    }
    else
        logg.logMessage("...Unable to set proc counters");
#endif /* CONFIG_SUPPORT_PROC_POLLING */

    return result;
}
