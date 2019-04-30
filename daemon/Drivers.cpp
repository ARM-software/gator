/**
 * Copyright (C) Arm Limited 2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Drivers.h"

#include "ICpuInfo.h"
#include "Logging.h"

static std::unique_ptr<PrimarySourceProvider> createPrimarySourceProvider(const char * module, bool systemWide,
                                                                          PmuXML && pmuXml, const char * maliFamilyName)
{
    std::unique_ptr<PrimarySourceProvider> primarySourceProvider = PrimarySourceProvider::detect(module, systemWide,
                                                                                                 std::move(pmuXml),
                                                                                                 maliFamilyName);
    if (!primarySourceProvider) {
        logg.logError(
                "Unable to initialize primary capture source:\n"
                "  >>> gator.ko should be co-located with gatord in the same directory\n"
                "  >>> OR insmod gator.ko prior to launching gatord\n"
                "  >>> OR specify the location of gator.ko on the command line\n"
                "  >>> OR run Linux 3.4 or later with perf (CONFIG_PERF_EVENTS and CONFIG_HW_PERF_EVENTS) and tracing (CONFIG_TRACING and CONFIG_CONTEXT_SWITCH_TRACER) support to collect data via userspace only");
        handleException();
    }

    return primarySourceProvider;
}

Drivers::Drivers(const char * module, bool systemWide, PmuXML && pmuXml,
                 const std::vector<std::string> userSpecifiedMaliDeviceTypes,
                 const std::vector<std::string> userSpecifiedMaliDevicePaths)
        : mMaliHwCntrs { userSpecifiedMaliDeviceTypes, userSpecifiedMaliDevicePaths },
          mPrimarySourceProvider { createPrimarySourceProvider(module, systemWide, std::move(pmuXml),
                                                               mMaliHwCntrs.getSupportedDeviceFamilyName()) },
          mMaliVideo { },
          mMidgard { },
          mFtraceDriver { !mPrimarySourceProvider->supportsTracepointCapture(), mPrimarySourceProvider->getCpuInfo().getCpuIds().size() },
          mAtraceDriver { mFtraceDriver },
          mTtraceDriver { mFtraceDriver },
          mExternalDriver { },
          mCcnDriver { },
          all { },
          allPolled { }
{
    all.push_back(&mPrimarySourceProvider->getPrimaryDriver());
    for (PolledDriver * driver : mPrimarySourceProvider->getAdditionalPolledDrivers()) {
        all.push_back(driver);
        allPolled.push_back(driver);
    }
    for (auto const& polledDriver : mMaliHwCntrs.getPolledDrivers()) {
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
