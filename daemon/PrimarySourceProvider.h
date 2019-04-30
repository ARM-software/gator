/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_PRIMARYSOURCEPROVIDER_H
#define INCLUDE_PRIMARYSOURCEPROVIDER_H

#include <cstdint>
#include <memory>
#include <semaphore.h>
#include <set>
#include <vector>

#include "ClassBoilerPlate.h"

class Child;
class Driver;
class PolledDriver;
class FtraceDriver;
class Source;
struct PmuXML;
class ICpuInfo;

/**
 * Interface for different primary source types.
 * Primary source types currently are:
 *
 *    - gator.ko
 *    - Linux perf API
 */
class PrimarySourceProvider
{
public:

    /**
     * Static initialization / detection
     */
    static std::unique_ptr<PrimarySourceProvider> detect(const char * module, bool systemWide, PmuXML && pmuXml, const char * maliFamilyName);

    virtual ~PrimarySourceProvider();

    /** Return the attribute value for captured.xml type attribute */
    virtual const char * getCaptureXmlTypeValue() const = 0;

    /** Return the backtrace_processing mode for captured.xml attribute */
    virtual const char * getBacktraceProcessingMode() const = 0;

    /** Return the monotonic timestamp the source started */
    virtual std::int64_t getMonotonicStarted() const = 0;

    /** Return true if the primary source is responsible for capturing tracepoints */
    virtual bool supportsTracepointCapture() const = 0;

    /** Return true if the primary source is responsible for capturing mali counters */
    virtual bool supportsMaliCapture() const = 0;

    /** Return true if the sample rate is supported for mali counters */
    virtual bool supportsMaliCaptureSampleRate(int rate) const = 0;

    /** Return true if the target is capturing mali counters */
    virtual bool isCapturingMaliCounters() const = 0;

    /** Return list of additional polled drivers required for source */
    virtual const std::vector<PolledDriver *> & getAdditionalPolledDrivers() const;

    /** Some driver specific message to show if prepare failed */
    virtual const char * getPrepareFailedMessage() const = 0;

    /** Return the primary driver object */
    virtual const Driver & getPrimaryDriver() const = 0;

    /** Return the primary driver object */
    virtual Driver & getPrimaryDriver() = 0;

    /** Create the primary Source instance */
    virtual std::unique_ptr<Source> createPrimarySource(Child & child, sem_t & senderSem, sem_t & startProfile,
                                                        const std::set<int> & appTids, FtraceDriver & ftraceDriver,
                                                        bool enableOnCommandExec) = 0;

    virtual const ICpuInfo & getCpuInfo() const = 0;
    virtual ICpuInfo & getCpuInfo() = 0;

protected:

    PrimarySourceProvider(const std::vector<PolledDriver *> & polledDrivers);

private:

    std::vector<PolledDriver *> polledDrivers;

    CLASS_DELETE_COPY_MOVE(PrimarySourceProvider);
};

#endif /* INCLUDE_PRIMARYSOURCEPROVIDER_H */
