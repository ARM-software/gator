/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#include "PrimarySourceProvider.h"
#include "DiskIODriver.h"
#include "DriverSource.h"
#include "FSDriver.h"
#include "KMod.h"
#include "HwmonDriver.h"
#include "Logging.h"
#include "MemInfoDriver.h"
#include "NetDriver.h"
#include "linux/perf/PerfDriver.h"
#include "linux/perf/PerfSource.h"
#include "PmuXML.h"
#include "SessionData.h"
#include "non_root/NonRootDriver.h"
#include "non_root/NonRootSource.h"

#include <unistd.h>

extern bool setupFilesystem(const char* module);

namespace
{
    /**
     * Primary source that reads from gator.ko
     */
    class GatorKoPrimarySource : public PrimarySourceProvider
    {
    public:

        static std::unique_ptr<PrimarySourceProvider> tryCreate(const char * module)
        {
            std::unique_ptr<PrimarySourceProvider> result;

            if (setupFilesystem(module)) {
                DriverSource::checkVersion();
                PmuXML::writeToKernel();

                result.reset(new GatorKoPrimarySource());
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
            if (DriverSource::readInt64Driver("/dev/gator/started", &monotonicStarted) == -1) {
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

        virtual std::unique_ptr<Source> createPrimarySource(Child & child, sem_t & senderSem,
                                                            sem_t & startProfile, const std::set<int> &) override
        {
            return std::unique_ptr<Source>(new DriverSource(child, senderSem, startProfile));
        }

    private:

        static std::vector<PolledDriver *> createPolledDrivers()
        {
            return std::vector<PolledDriver *> { { new HwmonDriver(), new FSDriver() } };
        }

        GatorKoPrimarySource()
                : PrimarySourceProvider(createPolledDrivers()),
                  driver()
        {
        }

        KMod driver;
    };

    /**
     * Primary source that reads from Linux perf API
     */
    class PerfPrimarySource : public PrimarySourceProvider
    {
    public:

        static std::unique_ptr<PrimarySourceProvider> tryCreate(bool systemWide)
        {
            std::unique_ptr<PrimarySourceProvider> result;

            std::unique_ptr<PerfDriver::PerfDriverConfiguration> configuration = PerfDriver::detect(systemWide);
            if (configuration != nullptr) {
                result.reset(new PerfPrimarySource(*configuration));
            }

            return result;
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

        virtual std::unique_ptr<Source> createPrimarySource(Child & child, sem_t & senderSem,
                                                            sem_t & startProfile, const std::set<int> & appTids) override
        {
            return std::unique_ptr<Source>(new PerfSource(driver, child, senderSem, startProfile, appTids));
        }

    private:

        static std::vector<PolledDriver *> createPolledDrivers()
        {
            return std::vector<PolledDriver *> { { new HwmonDriver(), new FSDriver(), new DiskIODriver(),
                                                   new MemInfoDriver(), new NetDriver() } };
        }

        PerfPrimarySource(const PerfDriver::PerfDriverConfiguration & configuration)
                : PrimarySourceProvider(createPolledDrivers()),
                  driver(configuration)
        {
        }

        PerfDriver driver;
    };

    /**
     * Primary source that reads from top-like information from /proc and works in non-root environment
     */
    class NonRootPrimarySource : public PrimarySourceProvider
    {
    public:

        static std::unique_ptr<PrimarySourceProvider> tryCreate()
        {
            return std::unique_ptr<PrimarySourceProvider>(new NonRootPrimarySource());
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

        virtual std::unique_ptr<Source> createPrimarySource(Child & child, sem_t & senderSem,
                                                            sem_t & startProfile, const std::set<int> &) override
        {
            return std::unique_ptr<Source>(new non_root::NonRootSource(driver, child, senderSem, startProfile));
        }

    private:

        static std::vector<PolledDriver *> createPolledDrivers()
        {
            return std::vector<PolledDriver *> { { new HwmonDriver(), new FSDriver(), new DiskIODriver(),
                                                   new MemInfoDriver(), new NetDriver() } };
        }

        NonRootPrimarySource()
                : PrimarySourceProvider(createPolledDrivers()),
                  driver()
        {
        }

        non_root::NonRootDriver driver;
    };
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

std::unique_ptr<PrimarySourceProvider> PrimarySourceProvider::detect(const char * module, bool systemWide)
{
    std::unique_ptr<PrimarySourceProvider> result;

    // Verify root permissions
    const bool isRoot = (geteuid() == 0);

    logg.logMessage("Determining primary source");

    // try gator.ko
    if (isRoot && systemWide) {
        logg.logMessage("Trying gator.ko...");
        result = GatorKoPrimarySource::tryCreate(module);
        if (result != nullptr) {
            logg.logMessage("...Success");
            logg.logSetup("Profiling Source\nUsing gator.ko for primary data source");
            return result;
        }
        else
            logg.logMessage("...Unable to set up gator.ko");
    }

    // try perf
    if (isRoot)
        logg.logMessage("Trying perf API as root...");
    else
        logg.logMessage("Trying perf API as non-root...");

    result = PerfPrimarySource::tryCreate(systemWide);
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

    // fall back to non-root mode
    if (isRoot)
        logg.logMessage("Trying proc counters as root...");
    else
        logg.logMessage("Trying proc counters as non-root; limited system profiling information available...");

    result = NonRootPrimarySource::tryCreate();
    if (result != nullptr) {
        logg.logMessage("...Success");
        logg.logSetup("Profiling Source\nUsing proc polling for primary data source");
        return result;
    }
    else
        logg.logMessage("...Unable to set proc counters");

    return nullptr;
}
