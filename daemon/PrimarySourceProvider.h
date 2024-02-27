/* Copyright (C) 2017-2024 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_PRIMARYSOURCEPROVIDER_H
#define INCLUDE_PRIMARYSOURCEPROVIDER_H

#include "Configuration.h"
#include "ISender.h"
#include "agents/agent_workers_process_holder.h"
#include "lib/Span.h"
#include "linux/perf/PerfEventGroupIdentifier.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <vector>

#include <semaphore.h>

class Child;
class Driver;
class PolledDriver;
class FtraceDriver;
class PrimarySource;
class ICpuInfo;
class UncorePmu;
struct PmuXML;
struct TraceFsConstants;

/**
 * Interface for different primary source types.
 * Primary source types currently are:
 *
 *    - Linux perf API
 *    - Non-root proc polling
 */
class PrimarySourceProvider {
public:
    /**
     * Static initialization / detection
     */
    static std::unique_ptr<PrimarySourceProvider> detect(CaptureOperationMode captureOperationMode,
                                                         const TraceFsConstants & traceFsConstants,
                                                         PmuXML && pmuXml,
                                                         const char * maliFamilyName,
                                                         bool disableCpuOnlining,
                                                         bool disableKernelAnnotations);

    PrimarySourceProvider(const PrimarySourceProvider &) = delete;
    PrimarySourceProvider & operator=(const PrimarySourceProvider &) = delete;
    PrimarySourceProvider(PrimarySourceProvider &&) = delete;
    PrimarySourceProvider & operator=(PrimarySourceProvider &&) = delete;

    virtual ~PrimarySourceProvider();

    /** Return the attribute value for captured.xml type attribute */
    [[nodiscard]] virtual const char * getCaptureXmlTypeValue() const = 0;

    /** Return the backtrace_processing mode for captured.xml attribute */
    [[nodiscard]] virtual const char * getBacktraceProcessingMode() const = 0;

    /** Return true if the primary source is responsible for capturing tracepoints */
    [[nodiscard]] virtual bool supportsTracepointCapture() const = 0;

    /** Return true if the FtraceDriver is responsible for capturing the cpu_frequency tracepoint */
    [[nodiscard]] virtual bool useFtraceDriverForCpuFrequency() const = 0;

    /** Return true if the source supports setting more than one EBS counter */
    [[nodiscard]] virtual bool supportsMultiEbs() const = 0;

    /** Return list of additional polled drivers required for source */
    [[nodiscard]] virtual const std::vector<PolledDriver *> & getAdditionalPolledDrivers() const;

    /** Some driver specific message to show if prepare failed */
    [[nodiscard]] virtual const char * getPrepareFailedMessage() const = 0;

    /** Return the primary driver object */
    [[nodiscard]] virtual const Driver & getPrimaryDriver() const = 0;

    /** Return the primary driver object */
    [[nodiscard]] virtual Driver & getPrimaryDriver() = 0;

    /** Create the primary Source instance */
    [[nodiscard]] virtual std::shared_ptr<PrimarySource> createPrimarySource(
        sem_t & senderSem,
        ISender & sender,
        std::function<bool()> session_ended_callback,
        std::function<void()> execTargetAppCallback,
        std::function<void()> profilingStartedCallback,
        const std::set<int> & appTids,
        FtraceDriver & ftraceDriver,
        bool enableOnCommandExec,
        agents::agent_workers_process_default_t & agent_workers_process) = 0;

    [[nodiscard]] virtual const ICpuInfo & getCpuInfo() const = 0;
    [[nodiscard]] virtual ICpuInfo & getCpuInfo() = 0;

    [[nodiscard]] virtual lib::Span<const UncorePmu> getDetectedUncorePmus() const = 0;

protected:
    explicit PrimarySourceProvider(std::vector<PolledDriver *> polledDrivers);

private:
    std::vector<PolledDriver *> polledDrivers;
};

#endif /* INCLUDE_PRIMARYSOURCEPROVIDER_H */
