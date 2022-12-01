/* Copyright (C) 2018-2022 by Arm Limited. All rights reserved. */

#ifndef DRIVERS_H
#define DRIVERS_H

#include "AtraceDriver.h"
#include "CCNDriver.h"
#include "ExternalDriver.h"
#include "FtraceDriver.h"
#include "MidgardDriver.h"
#include "PrimarySourceProvider.h"
#include "TtraceDriver.h"
#include "agents/perfetto/perfetto_driver.h"
#include "armnn/ArmNNDriver.h"
#include "lib/Span.h"
#include "linux/perf/PerfDriver.h"
#include "mali_userspace/MaliHwCntrDriver.h"

#include <vector>

class Drivers {
public:
    Drivers(bool systemWide,
            PmuXML && pmuXml,
            bool disableCpuOnlining,
            bool disableKernelAnnotations,
            const TraceFsConstants & traceFsConstants);

    Drivers(const Drivers &) = delete;
    Drivers & operator=(const Drivers &) = delete;
    Drivers(Drivers &&) = delete;
    Drivers & operator=(Drivers &&) = delete;

    armnn::Driver & getArmnnDriver() { return mArmnnDriver; }

    MidgardDriver & getMidgard() { return mMidgard; }

    CCNDriver & getCcnDriver() { return mCcnDriver; }

    FtraceDriver & getFtraceDriver() { return mFtraceDriver; }

    AtraceDriver & getAtraceDriver() { return mAtraceDriver; }

    TtraceDriver & getTtraceDriver() { return mTtraceDriver; }

    ExternalDriver & getExternalDriver() { return mExternalDriver; }

    agents::perfetto::perfetto_driver_t & getPerfettoDriver() { return mPerfettoDriver; }

    const PrimarySourceProvider & getPrimarySourceProvider() const { return *mPrimarySourceProvider; }

    PrimarySourceProvider & getPrimarySourceProvider() { return *mPrimarySourceProvider; }

    mali_userspace::MaliHwCntrDriver & getMaliHwCntrs() { return mMaliHwCntrs; }

    lib::Span<Driver * const> getAll() { return all; }

    lib::Span<const Driver * const> getAllConst() const { return all; }

    lib::Span<PolledDriver * const> getAllPolled() { return allPolled; }

    lib::Span<const PolledDriver * const> getAllPolledConst() const { return allPolled; }

private:
    mali_userspace::MaliHwCntrDriver mMaliHwCntrs {};
    std::unique_ptr<PrimarySourceProvider> mPrimarySourceProvider;
    MidgardDriver mMidgard {};
    ExternalDriver mExternalDriver {};
    CCNDriver mCcnDriver {};
    armnn::Driver mArmnnDriver {};
    // gator::android::ThermalDriver mThermalDriver{};
    FtraceDriver mFtraceDriver;
    AtraceDriver mAtraceDriver;
    TtraceDriver mTtraceDriver;
    agents::perfetto::perfetto_driver_t mPerfettoDriver;
    std::vector<Driver *> all {};
    std::vector<PolledDriver *> allPolled {};
};

#endif // DRIVERS_H
