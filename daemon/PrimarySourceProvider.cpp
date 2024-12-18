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
#include "lib/midr.h"
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

#include <algorithm>
#include <utility>

#include <unistd.h>

namespace {
    const char * CORE_NAME_UNKNOWN = "unknown";

    /// array for cpuIds and clusterIds
    class Ids {
    public:
        explicit Ids(unsigned int maxCoreNumber) : maxCoreNumber(maxCoreNumber)
        {
            for (unsigned int i = 0; i < maxCoreNumber * 2; ++i) {
                ids.get()[i] = -1; // Unknown
            }
        }

        [[nodiscard]] lib::Span<cpu_utils::midr_t> getMidrs()
        {
            return {reinterpret_cast<cpu_utils::midr_t *>(ids.get()), maxCoreNumber};
        }

        [[nodiscard]] lib::Span<const cpu_utils::midr_t> getMidrs() const
        {
            return {reinterpret_cast<cpu_utils::midr_t const *>(ids.get()), maxCoreNumber};
        }

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

        [[nodiscard]] lib::Span<const cpu_utils::midr_t> getMidrs() const override { return ids.getMidrs(); }

        [[nodiscard]] lib::Span<const GatorCpu> getClusters() const override { return clusters; }

        [[nodiscard]] lib::Span<const int> getClusterIds() const override { return ids.getClusterIds(); }

        [[nodiscard]] const char * getModelName() const override { return modelName.c_str(); }

        void updateIds(bool ignoreOffline) override
        {
            cpu_utils::readCpuInfo(disableCpuOnlining || ignoreOffline, false, ids.getMidrs());
            updateClusterIds();
        }

    private:
        Ids ids;
        std::vector<GatorCpu> clusters;
        std::string modelName;
        bool disableCpuOnlining;

        void updateClusterIds() { ICpuInfo::updateClusterIds(ids.getMidrs(), clusters, ids.getClusterIds()); }
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
                                                ids.getMidrs(),
                                                gSessionData.smmu_identifiers,
                                                pmuXml);

            if (configuration != nullptr) {
                if (!configuration->config.supports_inherit_sample_read
                    && captureOperationMode == CaptureOperationMode::application_experimental_patch) {
                    LOG_ERROR("Your kernel does not support the requested experimental inherit.\n Please "
                              "install the required kernel patch or choose a different inherit mode. ");
                    handleException();
                }

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
    const std::string hardwareName = cpu_utils::readCpuInfo(disableCpuOnlining, true, ids.getMidrs());
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

    return result;
}
