/* Copyright (C) 2018-2020 by Arm Limited. All rights reserved. */

#ifndef DRIVERS_H
#define DRIVERS_H

#include "AtraceDriver.h"
#include "CCNDriver.h"
#include "ExternalDriver.h"
#include "FtraceDriver.h"
#include "MaliVideoDriver.h"
#include "MidgardDriver.h"
#include "PrimarySourceProvider.h"
#include "TtraceDriver.h"
#include "lib/Span.h"
#include "linux/perf/PerfDriver.h"
#include "mali_userspace/MaliHwCntrDriver.h"

#include <vector>

class Drivers {
public:
    Drivers(bool systemWide, PmuXML && pmuXml);

    MidgardDriver & getMidgard() { return mMidgard; }

    MaliVideoDriver & getMaliVideo() { return mMaliVideo; }

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
    MaliVideoDriver mMaliVideo;
    MidgardDriver mMidgard;
    FtraceDriver mFtraceDriver;
    AtraceDriver mAtraceDriver;
    TtraceDriver mTtraceDriver;
    ExternalDriver mExternalDriver;
    CCNDriver mCcnDriver;
    std::vector<Driver *> all;
    std::vector<PolledDriver *> allPolled;

    Drivers(const Drivers &) = delete;
    Drivers & operator=(const Drivers &) = delete;
    Drivers(Drivers &&) = delete;
    Drivers & operator=(Drivers &&) = delete;
};

#endif // DRIVERS_H
