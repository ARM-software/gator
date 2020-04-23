/* Copyright (C) 2018-2020 by Arm Limited. All rights reserved. */

#include "Drivers.h"

#include "ICpuInfo.h"
#include "Logging.h"

static std::unique_ptr<PrimarySourceProvider> createPrimarySourceProvider(bool systemWide,
                                                                          PmuXML && pmuXml,
                                                                          const char * maliFamilyName)
{
    std::unique_ptr<PrimarySourceProvider> primarySourceProvider =
        PrimarySourceProvider::detect(systemWide, std::move(pmuXml), maliFamilyName);
    if (!primarySourceProvider) {
        logg.logError(
            "Unable to initialize primary capture source:\n"
            "  >>> Run Linux 3.4 or later with perf (CONFIG_PERF_EVENTS and CONFIG_HW_PERF_EVENTS) and tracing "
            "(CONFIG_TRACING and CONFIG_CONTEXT_SWITCH_TRACER) support to collect data via userspace only");
        handleException();
    }

    return primarySourceProvider;
}

Drivers::Drivers(bool systemWide, PmuXML && pmuXml)
    : mMaliHwCntrs{},
      mPrimarySourceProvider{
          createPrimarySourceProvider(systemWide, std::move(pmuXml), mMaliHwCntrs.getSupportedDeviceFamilyName())},
      mMaliVideo{},
      mMidgard{},
      mFtraceDriver{!mPrimarySourceProvider->supportsTracepointCapture(),
                    mPrimarySourceProvider->getCpuInfo().getCpuIds().size()},
      mAtraceDriver{mFtraceDriver},
      mTtraceDriver{mFtraceDriver},
      mExternalDriver{},
      mCcnDriver{},
      all{},
      allPolled{}
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
    all.push_back(&mMaliVideo);
    all.push_back(&mMaliHwCntrs);
    all.push_back(&mMidgard);
    all.push_back(&mFtraceDriver);
    all.push_back(&mFtraceDriver);
    all.push_back(&mAtraceDriver);
    all.push_back(&mTtraceDriver);
    all.push_back(&mExternalDriver);
    all.push_back(&mCcnDriver);
}
