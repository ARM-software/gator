/* Copyright (C) 2018-2024 by Arm Limited. All rights reserved. */

#include "Drivers.h"

#include "ICpuInfo.h"
#include "Logging.h"
#include "PolledDriver.h"
#include "PrimarySourceProvider.h"
#include "linux/Tracepoints.h"
#include "xml/EventsXML.h"
#include "xml/PmuXML.h"

#include <memory>
#include <utility>

static std::unique_ptr<PrimarySourceProvider> createPrimarySourceProvider(CaptureOperationMode captureOperationMode,
                                                                          const TraceFsConstants & traceFsConstants,
                                                                          PmuXML && pmuXml,
                                                                          const char * maliFamilyName,
                                                                          bool disableCpuOnlining,
                                                                          bool disableKernelAnnotations)
{

    std::unique_ptr<PrimarySourceProvider> primarySourceProvider =
        PrimarySourceProvider::detect(captureOperationMode,
                                      traceFsConstants,
                                      std::move(pmuXml),
                                      maliFamilyName,
                                      disableCpuOnlining,
                                      disableKernelAnnotations);
    if (!primarySourceProvider) {
        LOG_ERROR("Unable to initialize primary capture source:\n"
                  "  >>> Run Linux 3.4 or later with perf (CONFIG_PERF_EVENTS and CONFIG_HW_PERF_EVENTS) and tracing "
                  "(CONFIG_TRACING and CONFIG_CONTEXT_SWITCH_TRACER) support to collect data via userspace only");
        handleException();
    }

    return primarySourceProvider;
}

Drivers::Drivers(CaptureOperationMode captureOperationMode,
                 PmuXML && pmuXml,
                 bool disableCpuOnlining,
                 bool disableKernelAnnotations,
                 const TraceFsConstants & traceFsConstants)
    : mPrimarySourceProvider {createPrimarySourceProvider(captureOperationMode,
                                                          traceFsConstants,
                                                          std::move(pmuXml),
                                                          mMaliHwCntrs.getSupportedDeviceFamilyName(),
                                                          disableCpuOnlining,
                                                          disableKernelAnnotations)},

      mFtraceDriver {traceFsConstants,
                     !mPrimarySourceProvider->supportsTracepointCapture(),
                     mPrimarySourceProvider->useFtraceDriverForCpuFrequency(),
                     mPrimarySourceProvider->getCpuInfo().getMidrs().size()},
      mAtraceDriver {mFtraceDriver},
      mTtraceDriver {mFtraceDriver},
      mPerfettoDriver {mMaliHwCntrs.getSupportedDeviceFamilyName()}
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
    all.push_back(&mAtraceDriver);
    all.push_back(&mTtraceDriver);
    all.push_back(&mExternalDriver);
    all.push_back(&mCcnDriver);
    all.push_back(&mArmnnDriver);
    all.push_back(&mPerfettoDriver);

    auto staticEventsXml = events_xml::getStaticTree(mPrimarySourceProvider->getCpuInfo().getClusters(),
                                                     mPrimarySourceProvider->getDetectedUncorePmus());
    for (Driver * driver : all) {
        driver->readEvents(staticEventsXml.get());
    }
}
