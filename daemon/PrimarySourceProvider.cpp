/* Copyright (c) 2017 by ARM Limited. All rights reserved. */

#include "PrimarySourceProvider.h"
#include "DiskIODriver.h"
#include "DriverSource.h"
#include "FSDriver.h"
#include "KMod.h"
#include "HwmonDriver.h"
#include "Logging.h"
#include "MemInfoDriver.h"
#include "NetDriver.h"
#include "PerfDriver.h"
#include "PerfSource.h"
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
                                                            sem_t & startProfile) override
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

        static std::unique_ptr<PrimarySourceProvider> tryCreate()
        {
            std::unique_ptr<PrimarySourceProvider> result;

            std::unique_ptr<PerfDriver::PerfDriverConfiguration> configuration = PerfDriver::detect();
            if (configuration != nullptr) {
                result.reset(new PerfPrimarySource(*configuration));
            }

            return result;
        }

        virtual const char * getCaptureXmlTypeValue() const override
        {
            return "Perf";
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
                                                            sem_t & startProfile) override
        {
            return std::unique_ptr<Source>(new PerfSource(driver, child, senderSem, startProfile));
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
                                                            sem_t & startProfile) override
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

std::unique_ptr<PrimarySourceProvider> PrimarySourceProvider::detect(const char * module)
{
    std::unique_ptr<PrimarySourceProvider> result;

    // Verify root permissions
    const bool isRoot = (geteuid() == 0);

    // try gator.ko
    if (isRoot) {
        logg.logError("Trying gator.ko as primary source");
        result = GatorKoPrimarySource::tryCreate(module);
    }

    // try perf
    if (result == nullptr) {
        if (isRoot) {
            logg.logError("Unable to set up gatorfs, trying perf API");
            result = PerfPrimarySource::tryCreate();
        }
        else {
            // check if possible
            int perf_event_paranoid = 2;
            DriverSource::readIntDriver("/proc/sys/kernel/perf_event_paranoid", &perf_event_paranoid);

            // check can access /sys/kernel/debug and /sys/kernel/debug/tracing
            const bool canAccessEvents = (access("/sys/kernel/debug/tracing/events", R_OK) == 0);

            if ((perf_event_paranoid < 0) && canAccessEvents) {
                logg.logError("Trying perf API from userspace");
                result = PerfPrimarySource::tryCreate();
            }
            else {
                logg.logError("Perf API is not available to non-root users. \n"
                        "Please make sure '/proc/sys/kernel/perf_event_paranoid' is set -1, \n"
                        "and '/sys/kernel/debug' and '/sys/kernel/debug/tracing' are mounted and accessible as this user.\n\n"
                        "Try (as root):\n"
                        " - mount -o remount,mode=755 /sys/kernel/debug\n"
                        " - mount -o remount,mode=755 /sys/kernel/debug/tracing\n"
                        " - echo -1 > /proc/sys/kernel/perf_event_paranoid\n");
            }
        }
    }

    // fall back to non-root mode
    if (result == nullptr) {
        if (isRoot) {
            logg.logError("Unable to set up perf API, falling back to non-root counters");
        }
        else {
            logg.logError("gatord was launched with non-root privileges; limited system profiling information available");
        }

        result = NonRootPrimarySource::tryCreate();
    }

    return result;
}
