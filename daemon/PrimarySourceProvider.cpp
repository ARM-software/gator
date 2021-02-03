/* Copyright (C) 2017-2020 by Arm Limited. All rights reserved. */

#include "PrimarySourceProvider.h"

#include "Config.h"
#include "CpuUtils.h"
#include "DiskIODriver.h"
#include "FSDriver.h"
#include "HwmonDriver.h"
#include "ICpuInfo.h"
#include "Logging.h"
#include "MemInfoDriver.h"
#include "NetDriver.h"
#include "SessionData.h"
#include "lib/FsEntry.h"
#include "lib/Utils.h"
#include "xml/PmuXML.h"
#if CONFIG_SUPPORT_PERF
#include "linux/perf/PerfDriver.h"
#include "linux/perf/PerfDriverConfiguration.h"
#include "linux/perf/PerfSource.h"
#endif
#if CONFIG_SUPPORT_PROC_POLLING
#include "non_root/NonRootDriver.h"
#include "non_root/NonRootSource.h"
#endif

#include <algorithm>
#include <unistd.h>
#include <utility>

static const char CORE_NAME_UNKNOWN[] = "unknown";

namespace {
    /// array for cpuIds and clusterIds
    class Ids {
    public:
        Ids(unsigned int maxCoreNumber) : maxCoreNumber(maxCoreNumber)
        {
            for (unsigned int i = 0; i < maxCoreNumber * 2; ++i) {
                ids.get()[i] = -1; // Unknown
            }
        }

        lib::Span<int> getCpuIds() { return {ids.get(), maxCoreNumber}; }

        lib::Span<const int> getCpuIds() const { return {ids.get(), maxCoreNumber}; }

        lib::Span<int> getClusterIds() { return {ids.get() + maxCoreNumber, maxCoreNumber}; }

        lib::Span<const int> getClusterIds() const { return {ids.get() + maxCoreNumber, maxCoreNumber}; }

    private:
        unsigned int maxCoreNumber;
        shared_memory::unique_ptr<int[]> ids = shared_memory::make_unique<int[]>(maxCoreNumber * 2);
    };

    class CpuInfo : public ICpuInfo {
    public:
        CpuInfo(Ids && ids, std::vector<GatorCpu> && clusters, const char * modelName, bool disableCpuOnlining)
            : ids(std::move(ids)),
              clusters(std::move(clusters)),
              modelName(modelName),
              disableCpuOnlining(disableCpuOnlining)
        {
            std::sort(this->clusters.begin(), this->clusters.end());
            updateClusterIds();
        }

        virtual lib::Span<const int> getCpuIds() const override { return ids.getCpuIds(); }

        virtual lib::Span<const GatorCpu> getClusters() const override { return clusters; }

        virtual lib::Span<const int> getClusterIds() const override { return ids.getClusterIds(); }

        virtual void updateIds(bool ignoreOffline) override
        {
            cpu_utils::readCpuInfo(disableCpuOnlining || ignoreOffline, ids.getCpuIds());
            updateClusterIds();
        }

        virtual const char * getModelName() const override { return modelName.c_str(); }

        void updateClusterIds()
        {
            int lastClusterId = 0;
            for (size_t i = 0; i < ids.getCpuIds().length; ++i) {
                int clusterId = -1;
                for (size_t j = 0; j < clusters.size(); ++j) {
                    if (clusters[j].hasCpuId(ids.getCpuIds()[i])) {
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
        bool disableCpuOnlining;
    };

#if CONFIG_SUPPORT_PERF
    /**
     * Primary source that reads from Linux perf API
     */
    class PerfPrimarySource : public PrimarySourceProvider {
    public:
        /**
         *
         * @param systemWide
         * @param pmuXml consumes this on success
         * @return
         */
        static std::unique_ptr<PrimarySourceProvider> tryCreate(bool systemWide,
                                                                const TraceFsConstants & traceFsConstants,
                                                                PmuXML & pmuXml,
                                                                const char * maliFamilyName,
                                                                Ids & ids,
                                                                const char * modelName,
                                                                bool disableCpuOnlining)
        {
            std::unique_ptr<PerfDriverConfiguration> configuration =
                PerfDriverConfiguration::detect(systemWide, traceFsConstants.path__events, ids.getCpuIds(), pmuXml);
            if (configuration != nullptr) {
                std::vector<GatorCpu> clusters;
                for (const auto & perfCpu : configuration->cpus) {
                    clusters.push_back(perfCpu.gator_cpu);
                }
                CpuInfo cpuInfo {std::move(ids), std::move(clusters), modelName, disableCpuOnlining};
                return std::unique_ptr<PrimarySourceProvider> {new PerfPrimarySource(std::move(*configuration),
                                                                                     std::move(pmuXml),
                                                                                     maliFamilyName,
                                                                                     std::move(cpuInfo),
                                                                                     traceFsConstants)};
            }

            return nullptr;
        }

        virtual const char * getCaptureXmlTypeValue() const override { return "Perf"; }

        virtual const char * getBacktraceProcessingMode() const override { return "perf"; }

        virtual bool supportsTracepointCapture() const override { return true; }

        virtual bool supportsMultiEbs() const override { return true; }

        virtual const char * getPrepareFailedMessage() const override
        {
            return "Unable to communicate with the perf API, please ensure that CONFIG_TRACING and "
                   "CONFIG_CONTEXT_SWITCH_TRACER are enabled. Please refer to streamline/gator/README.md for more "
                   "information.";
        }

        virtual const Driver & getPrimaryDriver() const override { return driver; }

        virtual Driver & getPrimaryDriver() override { return driver; }

        virtual const ICpuInfo & getCpuInfo() const override { return cpuInfo; }

        virtual ICpuInfo & getCpuInfo() override { return cpuInfo; }

        virtual std::unique_ptr<PrimarySource> createPrimarySource(sem_t & senderSem,
                                                                   std::function<void()> profilingStartedCallback,
                                                                   const std::set<int> & appTids,
                                                                   FtraceDriver & ftraceDriver,
                                                                   bool enableOnCommandExec) override
        {
            auto source = std::unique_ptr<PerfSource>(new PerfSource(driver,
                                                                     senderSem,
                                                                     profilingStartedCallback,
                                                                     appTids,
                                                                     ftraceDriver,
                                                                     enableOnCommandExec,
                                                                     cpuInfo));
            if (!source->prepare()) {
                return {};
            }
            return source;
        }

    private:
        static std::vector<PolledDriver *> createPolledDrivers()
        {
            return std::vector<PolledDriver *> {
                {new HwmonDriver(), new FSDriver(), new DiskIODriver(), new MemInfoDriver(), new NetDriver()}};
        }

        PerfPrimarySource(PerfDriverConfiguration && configuration,
                          PmuXML && pmuXml,
                          const char * maliFamilyName,
                          CpuInfo && cpuInfo,
                          const TraceFsConstants & traceFsConstants)
            : PrimarySourceProvider(createPolledDrivers()),
              cpuInfo(std::move(cpuInfo)),
              driver(std::move(configuration), std::move(pmuXml), maliFamilyName, this->cpuInfo, traceFsConstants)
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
    class NonRootPrimarySource : public PrimarySourceProvider {
    public:
        static std::unique_ptr<PrimarySourceProvider> tryCreate(PmuXML && pmuXml,
                                                                Ids & ids,
                                                                const char * modelName,
                                                                bool disableCpuOnlining)
        {
            // detect clusters so we can generate activity events
            std::set<int> added;
            std::vector<GatorCpu> clusters;
            for (int cpuId : ids.getClusterIds()) {
                if (cpuId >= 0 && added.count(cpuId) != 0) {
                    const GatorCpu * gatorCpu = pmuXml.findCpuById(cpuId);
                    if (gatorCpu != nullptr) {
                        // create the cluster
                        added.insert(cpuId);
                        clusters.push_back(*gatorCpu);
                    }
                }
            }

            if (clusters.empty()) {
#if defined(__aarch64__)
                clusters.emplace_back("Other", "Other", "Other", nullptr, nullptr, std::set<int> {0xfffff}, 6, true);
#elif defined(__arm__)
                clusters.emplace_back("Other", "Other", "Other", nullptr, nullptr, std::set<int> {0xfffff}, 6, false);
#else
                clusters.emplace_back("Other",
                                      "Perf_Hardware",
                                      "Perf_Hardware",
                                      nullptr,
                                      nullptr,
                                      std::set<int> {0xfffff},
                                      6,
                                      false);
#endif
            }

            return std::unique_ptr<PrimarySourceProvider>(
                new NonRootPrimarySource(std::move(pmuXml),
                                         {std::move(ids), std::move(clusters), modelName, disableCpuOnlining}));
        }

        virtual const char * getCaptureXmlTypeValue() const override
        {
            // Sends data in gator format
            return "Gator";
        }

        virtual const char * getBacktraceProcessingMode() const override { return "none"; }

        virtual bool supportsTracepointCapture() const override { return true; }

        virtual bool supportsMultiEbs() const override { return false; }

        virtual const char * getPrepareFailedMessage() const override
        {
            return "Could not initialize /proc data capture";
        }

        virtual const Driver & getPrimaryDriver() const override { return driver; }

        virtual Driver & getPrimaryDriver() override { return driver; }

        virtual const ICpuInfo & getCpuInfo() const override { return cpuInfo; }

        virtual ICpuInfo & getCpuInfo() override { return cpuInfo; }

        virtual std::unique_ptr<PrimarySource> createPrimarySource(sem_t & senderSem,
                                                                   std::function<void()> profilingStartedCallback,
                                                                   const std::set<int> & /*appTids*/,
                                                                   FtraceDriver & /*ftraceDriver*/,
                                                                   bool /*enableOnCommandExec*/) override
        {
            return std::unique_ptr<PrimarySource>(
                new non_root::NonRootSource(driver, senderSem, profilingStartedCallback, cpuInfo));
        }

    private:
        static std::vector<PolledDriver *> createPolledDrivers()
        {
            return std::vector<PolledDriver *> {
                {new HwmonDriver(), new FSDriver(), new DiskIODriver(), new MemInfoDriver(), new NetDriver()}};
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

PrimarySourceProvider::PrimarySourceProvider(std::vector<PolledDriver *> polledDrivers_)
    : polledDrivers(std::move(polledDrivers_))
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

std::unique_ptr<PrimarySourceProvider> PrimarySourceProvider::detect(bool systemWide,
                                                                     const TraceFsConstants & traceFsConstants,
                                                                     PmuXML && pmuXml,
                                                                     const char * maliFamilyName,
                                                                     bool disableCpuOnlining)
{
    Ids ids {cpu_utils::getMaxCoreNum()};
    const std::string modelName = lib::FsEntry::create("/proc/device-tree/model").readFileContents();
    const std::string hardwareName = cpu_utils::readCpuInfo(disableCpuOnlining, ids.getCpuIds());
    const char * modelNameToUse =
        !modelName.empty() ? modelName.c_str() : !hardwareName.empty() ? hardwareName.c_str() : CORE_NAME_UNKNOWN;
    std::unique_ptr<PrimarySourceProvider> result;

    // Verify root permissions
    const bool isRoot = (geteuid() == 0);

    logg.logMessage("Determining primary source");

    // try perf
#if CONFIG_SUPPORT_PERF
    if (isRoot) {
        logg.logMessage("Trying perf API as root...");
    }
    else {
        logg.logMessage("Trying perf API as non-root...");
    }

    result = PerfPrimarySource::tryCreate(systemWide,
                                          traceFsConstants,
                                          pmuXml,
                                          maliFamilyName,
                                          ids,
                                          modelNameToUse,
                                          disableCpuOnlining);
    if (result != nullptr) {
        logg.logMessage("...Success");
        logg.logSetup("Profiling Source\nUsing perf API for primary data source");
        return result;
    }
    else {
        logg.logError("...Perf API is not available.");
    }
#endif /* CONFIG_SUPPORT_PERF */

    // fall back to proc mode
#if CONFIG_SUPPORT_PROC_POLLING
    if (isRoot) {
        logg.logMessage("Trying /proc counters as root...");
    }
    else {
        logg.logMessage("Trying /proc counters as non-root; limited system profiling information available...");
    }

    result = NonRootPrimarySource::tryCreate(std::move(pmuXml), ids, modelNameToUse, disableCpuOnlining);
    if (result != nullptr) {
        logg.logMessage("...Success");
        logg.logSetup("Profiling Source\nUsing /proc polling for primary data source");
        logg.logError(
            "Using deprecated /proc polling for primary data source. In future only perf API will be supported.");
        return result;
    }
    else {
        logg.logMessage("...Unable to set /proc counters");
    }
#endif /* CONFIG_SUPPORT_PROC_POLLING */

    return result;
}
