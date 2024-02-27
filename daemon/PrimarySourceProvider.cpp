/* Copyright (C) 2017-2024 by Arm Limited. All rights reserved. */

#include "PrimarySourceProvider.h"

#include "Config.h"
#include "Configuration.h"
#include "CpuUtils.h"
#include "DiskIODriver.h"
#include "FSDriver.h"
#include "HwmonDriver.h"
#include "ICpuInfo.h"
#include "ISender.h"
#include "Logging.h"
#include "MemInfoDriver.h"
#include "NetDriver.h"
#include "SessionData.h"
#include "agents/agent_workers_process_holder.h"
#include "lib/FsEntry.h"
#include "lib/SharedMemory.h"
#include "lib/Span.h"
#include "xml/PmuXML.h"

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <android/ThermalDriver.h>
#include <semaphore.h>
#if CONFIG_SUPPORT_PERF
#include "linux/perf/PerfDriver.h"
#include "linux/perf/PerfDriverConfiguration.h"
#endif
#if CONFIG_SUPPORT_PROC_POLLING
#include "non_root/NonRootDriver.h"
#include "non_root/NonRootSource.h"
#endif

#include <algorithm>
#include <utility>

#include <unistd.h>

static const char CORE_NAME_UNKNOWN[] = "unknown";

namespace {
    /// array for cpuIds and clusterIds
    class Ids {
    public:
        explicit Ids(unsigned int maxCoreNumber) : maxCoreNumber(maxCoreNumber)
        {
            for (unsigned int i = 0; i < maxCoreNumber * 2; ++i) {
                ids.get()[i] = -1; // Unknown
            }
        }

        [[nodiscard]] lib::Span<int> getCpuIds() { return {ids.get(), maxCoreNumber}; }

        [[nodiscard]] lib::Span<const int> getCpuIds() const { return {ids.get(), maxCoreNumber}; }

        [[nodiscard]] lib::Span<int> getClusterIds() { return {ids.get() + maxCoreNumber, maxCoreNumber}; }

        [[nodiscard]] lib::Span<const int> getClusterIds() const { return {ids.get() + maxCoreNumber, maxCoreNumber}; }

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

        [[nodiscard]] lib::Span<const int> getCpuIds() const override { return ids.getCpuIds(); }

        [[nodiscard]] lib::Span<const GatorCpu> getClusters() const override { return clusters; }

        [[nodiscard]] lib::Span<const int> getClusterIds() const override { return ids.getClusterIds(); }

        [[nodiscard]] const char * getModelName() const override { return modelName.c_str(); }

        void updateIds(bool ignoreOffline) override
        {
            cpu_utils::readCpuInfo(disableCpuOnlining || ignoreOffline, false, ids.getCpuIds());
            updateClusterIds();
        }

    private:
        Ids ids;
        std::vector<GatorCpu> clusters;
        std::string modelName;
        bool disableCpuOnlining;

        void updateClusterIds() { ICpuInfo::updateClusterIds(ids.getCpuIds(), clusters, ids.getClusterIds()); }
    };

#if CONFIG_SUPPORT_PERF
    /**
     * Primary source that reads from Linux perf API
     */
    class PerfPrimarySource : public PrimarySourceProvider {
    public:
        /**
         * @param pmuXml consumes this on success
         */
        static std::unique_ptr<PrimarySourceProvider> tryCreate(CaptureOperationMode captureOperationMode,
                                                                const TraceFsConstants & traceFsConstants,
                                                                PmuXML & pmuXml,
                                                                const char * maliFamilyName,
                                                                Ids & ids,
                                                                const char * modelName,
                                                                bool disableCpuOnlining,
                                                                bool disableKernelAnnotations)
        {
            std::unique_ptr<PerfDriverConfiguration> configuration =
                PerfDriverConfiguration::detect(captureOperationMode,
                                                traceFsConstants.path__events,
                                                ids.getCpuIds(),
                                                gSessionData.smmu_identifiers,
                                                pmuXml);
            if (configuration != nullptr) {
                // build the cpuinfo
                std::vector<GatorCpu> clusters;
                for (const auto & perfCpu : configuration->cpus) {
                    clusters.push_back(perfCpu.gator_cpu);
                }
                CpuInfo cpuInfo {std::move(ids), std::move(clusters), modelName, disableCpuOnlining};

                // build the uncorepmus list
                std::vector<UncorePmu> uncorePmus;
                for (const auto & uncore : configuration->uncores) {
                    uncorePmus.push_back(uncore.uncore_pmu);
                }

                return std::unique_ptr<PrimarySourceProvider> {new PerfPrimarySource(std::move(*configuration),
                                                                                     std::move(pmuXml),
                                                                                     maliFamilyName,
                                                                                     std::move(cpuInfo),
                                                                                     std::move(uncorePmus),
                                                                                     traceFsConstants,
                                                                                     disableKernelAnnotations)};
            }

            return nullptr;
        }

        [[nodiscard]] const char * getCaptureXmlTypeValue() const override { return "Perf"; }

        [[nodiscard]] const char * getBacktraceProcessingMode() const override { return "perf"; }

        [[nodiscard]] bool supportsTracepointCapture() const override { return true; }

        [[nodiscard]] bool useFtraceDriverForCpuFrequency() const override
        {
            return driver.getConfig().use_ftrace_for_cpu_frequency;
        }

        [[nodiscard]] bool supportsMultiEbs() const override { return true; }

        [[nodiscard]] const char * getPrepareFailedMessage() const override
        {
            return "Unable to communicate with the perf API, please ensure that CONFIG_TRACING and "
                   "CONFIG_CONTEXT_SWITCH_TRACER are enabled. Please refer to streamline/gator/README.md for more "
                   "information.";
        }

        [[nodiscard]] const Driver & getPrimaryDriver() const override { return driver; }

        Driver & getPrimaryDriver() override { return driver; }

        [[nodiscard]] const ICpuInfo & getCpuInfo() const override { return cpuInfo; }

        ICpuInfo & getCpuInfo() override { return cpuInfo; }

        [[nodiscard]] lib::Span<const UncorePmu> getDetectedUncorePmus() const override { return uncorePmus; }

        std::shared_ptr<PrimarySource> createPrimarySource(
            sem_t & senderSem,
            ISender & sender,
            std::function<bool()> session_ended_callback,
            std::function<void()> execTargetAppCallback,
            std::function<void()> profilingStartedCallback,
            const std::set<int> & appTids,
            FtraceDriver & ftraceDriver,
            bool enableOnCommandExec,
            agents::agent_workers_process_default_t & agent_workers_process) override
        {
            return driver.create_source(senderSem,
                                        sender,
                                        std::move(session_ended_callback),
                                        std::move(execTargetAppCallback),
                                        std::move(profilingStartedCallback),
                                        appTids,
                                        ftraceDriver,
                                        enableOnCommandExec,
                                        cpuInfo,
                                        uncorePmus,
                                        agent_workers_process);
        }

    private:
        static std::vector<PolledDriver *> createPolledDrivers()
        {
            return std::vector<PolledDriver *> {{new HwmonDriver(),
                                                 new FSDriver(),
                                                 new DiskIODriver(),
                                                 new MemInfoDriver(),
                                                 new NetDriver(),
                                                 new gator::android::ThermalDriver}};
        }

        PerfPrimarySource(PerfDriverConfiguration && configuration,
                          PmuXML && pmuXml,
                          const char * maliFamilyName,
                          CpuInfo && cpuInfo,
                          std::vector<UncorePmu> uncorePmus,
                          const TraceFsConstants & traceFsConstants,
                          bool disableKernelAnnotations)
            : PrimarySourceProvider(createPolledDrivers()),
              cpuInfo(std::move(cpuInfo)),
              driver(std::move(configuration),
                     std::move(pmuXml),
                     maliFamilyName,
                     this->cpuInfo,
                     traceFsConstants,
                     disableKernelAnnotations),
              uncorePmus(std::move(uncorePmus))
        {
        }

        CpuInfo cpuInfo;
        PerfDriver driver;
        std::vector<UncorePmu> uncorePmus;
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

        [[nodiscard]] const char * getCaptureXmlTypeValue() const override
        {
            // Sends data in gator format
            return "Gator";
        }

        [[nodiscard]] const char * getBacktraceProcessingMode() const override { return "none"; }

        [[nodiscard]] bool supportsTracepointCapture() const override { return false; }

        [[nodiscard]] bool useFtraceDriverForCpuFrequency() const override { return true; }

        [[nodiscard]] bool supportsMultiEbs() const override { return false; }

        [[nodiscard]] const char * getPrepareFailedMessage() const override
        {
            return "Could not initialize /proc data capture";
        }

        [[nodiscard]] const Driver & getPrimaryDriver() const override { return driver; }

        Driver & getPrimaryDriver() override { return driver; }

        [[nodiscard]] const ICpuInfo & getCpuInfo() const override { return cpuInfo; }

        ICpuInfo & getCpuInfo() override { return cpuInfo; }

        [[nodiscard]] lib::Span<const UncorePmu> getDetectedUncorePmus() const override { return {}; }

        std::unique_ptr<PrimarySource> createPrimarySource(
            sem_t & senderSem,
            ISender & /*sender*/,
            std::function<bool()> /*session_ended_callback*/,
            std::function<void()> execTargetAppCallback,
            std::function<void()> profilingStartedCallback,
            const std::set<int> & /*appTids*/,
            FtraceDriver & /*ftraceDriver*/,
            bool /*enableOnCommandExec*/,
            agents::agent_workers_process_default_t & /*agent_workers_process*/) override
        {
            return std::unique_ptr<PrimarySource>(new non_root::NonRootSource(driver,
                                                                              senderSem,
                                                                              std::move(execTargetAppCallback),
                                                                              std::move(profilingStartedCallback),
                                                                              cpuInfo));
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

std::unique_ptr<PrimarySourceProvider> PrimarySourceProvider::detect(CaptureOperationMode captureOperationMode,
                                                                     const TraceFsConstants & traceFsConstants,
                                                                     PmuXML && pmuXml,
                                                                     const char * maliFamilyName,
                                                                     bool disableCpuOnlining,
                                                                     bool disableKernelAnnotations)
{
    Ids ids {cpu_utils::getMaxCoreNum()};
    const std::string modelName = lib::FsEntry::create("/proc/device-tree/model").readFileContents();
    const std::string hardwareName = cpu_utils::readCpuInfo(disableCpuOnlining, true, ids.getCpuIds());
    const char * modelNameToUse = !modelName.empty()    ? modelName.c_str()
                                : !hardwareName.empty() ? hardwareName.c_str()
                                                        : CORE_NAME_UNKNOWN;
    std::unique_ptr<PrimarySourceProvider> result;

    // Verify root permissions
    const bool isRoot = (geteuid() == 0);

    LOG_FINE("Determining primary source");

    // try perf
#if CONFIG_SUPPORT_PERF
    if (isRoot) {
        LOG_FINE("Trying perf API as root...");
    }
    else {
        LOG_FINE("Trying perf API as non-root...");
    }

    result = PerfPrimarySource::tryCreate(captureOperationMode,
                                          traceFsConstants,
                                          pmuXml,
                                          maliFamilyName,
                                          ids,
                                          modelNameToUse,
                                          disableCpuOnlining,
                                          disableKernelAnnotations);
    if (result != nullptr) {
        LOG_FINE("...Success");
        LOG_SETUP("Profiling Source\nUsing perf API for primary data source");
        return result;
    }
    LOG_ERROR("...Perf API is not available.");

#endif /* CONFIG_SUPPORT_PERF */

    // fall back to proc mode
#if CONFIG_SUPPORT_PROC_POLLING
    if (isRoot) {
        LOG_FINE("Trying /proc counters as root...");
    }
    else {
        LOG_FINE("Trying /proc counters as non-root; limited system profiling information available...");
    }

    result = NonRootPrimarySource::tryCreate(std::move(pmuXml), ids, modelNameToUse, disableCpuOnlining);
    if (result != nullptr) {
        LOG_FINE("...Success");
        LOG_SETUP("Profiling Source\nUsing /proc polling for primary data source");
        LOG_ERROR("Using deprecated /proc polling for primary data source. In future only perf API will be supported.");
        return result;
    }
    LOG_WARNING("...Unable to set /proc counters");

#endif /* CONFIG_SUPPORT_PROC_POLLING */

    return result;
}
