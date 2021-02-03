/* Copyright (C) 2018-2020 by Arm Limited. All rights reserved. */

#ifndef DRIVERS_H
#define DRIVERS_H

#include "AtraceDriver.h"
#include "CCNDriver.h"
#include "ExternalDriver.h"
#include "FtraceDriver.h"
#include "MidgardDriver.h"
#include "PrimarySourceProvider.h"
#include "TtraceDriver.h"
#include "armnn/ArmNNDriver.h"
#include "lib/Span.h"
#include "linux/perf/PerfDriver.h"
#include "mali_userspace/MaliHwCntrDriver.h"

#include <vector>

class Drivers {
public:
    Drivers(bool systemWide, PmuXML && pmuXml, bool disableCpuOnlining, const TraceFsConstants & traceFsConstants);

    armnn::Driver & getArmnnDriver() { return mArmnnDriver; }

    MidgardDriver & getMidgard() { return mMidgard; }

    CCNDriver & getCcnDriver() { return mCcnDriver; }

    FtraceDriver & getFtraceDriver() { return mFtraceDriver; }

    AtraceDriver & getAtraceDriver() { return mAtraceDriver; }

    TtraceDriver & getTtraceDriver() { return mTtraceDriver; }

    ExternalDriver & getExternalDriver() { return mExternalDriver; }

    const PrimarySourceProvider & getPrimarySourceProvider() const { return *mPrimarySourceProvider; }

    PrimarySourceProvider & getPrimarySourceProvider() { return *mPrimarySourceProvider; }

    mali_userspace::MaliHwCntrDriver & getMaliHwCntrs() { return mMaliHwCntrs; }

    lib::Span<Driver * const> getAll() { return all; }

    lib::Span<const Driver * const> getAllConst() const { return all; }

    lib::Span<PolledDriver * const> getAllPolled() { return allPolled; }

    lib::Span<const PolledDriver * const> getAllPolledConst() const { return allPolled; }

private:
    mali_userspace::MaliHwCntrDriver mMaliHwCntrs;
    std::unique_ptr<PrimarySourceProvider> mPrimarySourceProvider;
    MidgardDriver mMidgard;
    FtraceDriver mFtraceDriver;
    AtraceDriver mAtraceDriver;
    TtraceDriver mTtraceDriver;
    ExternalDriver mExternalDriver;
    CCNDriver mCcnDriver;
    armnn::Driver mArmnnDriver;
    std::vector<Driver *> all;
    std::vector<PolledDriver *> allPolled;

    Drivers(const Drivers &) = delete;
    Drivers & operator=(const Drivers &) = delete;
    Drivers(Drivers &&) = delete;
    Drivers & operator=(Drivers &&) = delete;
};

#endif // DRIVERS_H
