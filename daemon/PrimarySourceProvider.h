/* Copyright (C) 2017-2021 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_PRIMARYSOURCEPROVIDER_H
#define INCLUDE_PRIMARYSOURCEPROVIDER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <semaphore.h>
#include <set>
#include <vector>

class Child;
class Driver;
class PolledDriver;
class FtraceDriver;
class PrimarySource;
struct PmuXML;
class ICpuInfo;
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
    static std::unique_ptr<PrimarySourceProvider> detect(bool systemWide,
                                                         const TraceFsConstants & traceFsConstants,
                                                         PmuXML && pmuXml,
                                                         const char * maliFamilyName,
                                                         bool disableCpuOnlining,
                                                         bool disableKernelAnnotations);

    virtual ~PrimarySourceProvider();

    /** Return the attribute value for captured.xml type attribute */
    virtual const char * getCaptureXmlTypeValue() const = 0;

    /** Return the backtrace_processing mode for captured.xml attribute */
    virtual const char * getBacktraceProcessingMode() const = 0;

    /** Return true if the primary source is responsible for capturing tracepoints */
    virtual bool supportsTracepointCapture() const = 0;

    /** Return true if the source supports setting more than one EBS counter */
    virtual bool supportsMultiEbs() const = 0;

    /** Return list of additional polled drivers required for source */
    virtual const std::vector<PolledDriver *> & getAdditionalPolledDrivers() const;

    /** Some driver specific message to show if prepare failed */
    virtual const char * getPrepareFailedMessage() const = 0;

    /** Return the primary driver object */
    virtual const Driver & getPrimaryDriver() const = 0;

    /** Return the primary driver object */
    virtual Driver & getPrimaryDriver() = 0;

    /** Create the primary Source instance */
    virtual std::unique_ptr<PrimarySource> createPrimarySource(sem_t & senderSem,
                                                               std::function<void()> profilingStartedCallback,
                                                               const std::set<int> & appTids,
                                                               FtraceDriver & ftraceDriver,
                                                               bool enableOnCommandExec) = 0;

    virtual const ICpuInfo & getCpuInfo() const = 0;
    virtual ICpuInfo & getCpuInfo() = 0;

protected:
    PrimarySourceProvider(std::vector<PolledDriver *> polledDrivers);

private:
    std::vector<PolledDriver *> polledDrivers;

    PrimarySourceProvider(const PrimarySourceProvider &) = delete;
    PrimarySourceProvider & operator=(const PrimarySourceProvider &) = delete;
    PrimarySourceProvider(PrimarySourceProvider &&) = delete;
    PrimarySourceProvider & operator=(PrimarySourceProvider &&) = delete;
};

#endif /* INCLUDE_PRIMARYSOURCEPROVIDER_H */
