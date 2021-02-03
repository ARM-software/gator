/* Copyright (C) 2018-2020 by Arm Limited. All rights reserved. */

#include "Drivers.h"

#include "ICpuInfo.h"
#include "Logging.h"
#include "xml/EventsXML.h"

static std::unique_ptr<PrimarySourceProvider> createPrimarySourceProvider(bool systemWide,
                                                                          const TraceFsConstants & traceFsConstants,
                                                                          PmuXML && pmuXml,
                                                                          const char * maliFamilyName,
                                                                          bool disableCpuOnlining)
{
    std::unique_ptr<PrimarySourceProvider> primarySourceProvider = PrimarySourceProvider::detect(systemWide,
                                                                                                 traceFsConstants,
                                                                                                 std::move(pmuXml),
                                                                                                 maliFamilyName,
                                                                                                 disableCpuOnlining);
    if (!primarySourceProvider) {
        logg.logError(
            "Unable to initialize primary capture source:\n"
            "  >>> Run Linux 3.4 or later with perf (CONFIG_PERF_EVENTS and CONFIG_HW_PERF_EVENTS) and tracing "
            "(CONFIG_TRACING and CONFIG_CONTEXT_SWITCH_TRACER) support to collect data via userspace only");
        handleException();
    }

    return primarySourceProvider;
}

Drivers::Drivers(bool systemWide, PmuXML && pmuXml, bool disableCpuOnlining, const TraceFsConstants & traceFsConstants)
    : mMaliHwCntrs {},
      mPrimarySourceProvider {createPrimarySourceProvider(systemWide,
                                                          traceFsConstants,
                                                          std::move(pmuXml),
                                                          mMaliHwCntrs.getSupportedDeviceFamilyName(),
                                                          disableCpuOnlining)},
      mMidgard {},
      mFtraceDriver {traceFsConstants,
                     !mPrimarySourceProvider->supportsTracepointCapture(),
                     mPrimarySourceProvider->getCpuInfo().getCpuIds().size()},
      mAtraceDriver {mFtraceDriver},
      mTtraceDriver {mFtraceDriver},
      mExternalDriver {},
      mCcnDriver {},
      mArmnnDriver {},
      all {},
      allPolled {}
{
    all.push_back(&mPrimarySourceProvider->getPrimaryDriver());
    for (PolledDriver * driver : mPrimarySourceProvider->getAdditionalPolledDrivers()) {
        all.push_back(driver);
        allPolled.push_back(driver);
    }
    for (auto const & polledDriver : mMaliHwCntrs.getPolledDrivers()) {
        all.push_back(polledDriver.second.get());
        allPolled.push_back(polledDriver.second.get());
    }
    all.push_back(&mMaliHwCntrs);
    all.push_back(&mMidgard);
    all.push_back(&mFtraceDriver);
    all.push_back(&mFtraceDriver);
    all.push_back(&mAtraceDriver);
    all.push_back(&mTtraceDriver);
    all.push_back(&mExternalDriver);
    all.push_back(&mCcnDriver);
    all.push_back(&mArmnnDriver);

    auto staticEventsXml = events_xml::getStaticTree(mPrimarySourceProvider->getCpuInfo().getClusters());
    for (Driver * driver : all) {
        driver->readEvents(staticEventsXml.get());
    }
}
