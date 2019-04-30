/**
 * Copyright (C) Arm Limited 2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef DRIVERS_H
#define DRIVERS_H

#include <vector>

#include "lib/Span.h"

#include "AtraceDriver.h"
#include "CCNDriver.h"
#include "ExternalDriver.h"
#include "FtraceDriver.h"
#include "KMod.h"
#include "MaliVideoDriver.h"
#include "MidgardDriver.h"
#include "linux/perf/PerfDriver.h"
#include "TtraceDriver.h"
#include "mali_userspace/MaliHwCntrDriver.h"
#include "PrimarySourceProvider.h"

#include "ClassBoilerPlate.h"

class Drivers
{
public:
    Drivers(const char * module, bool systemWide, PmuXML && pmuXml,
            const std::vector<std::string> userSpecifiedMaliDeviceTypes,
            const std::vector<std::string> userSpecifiedMaliDevicePaths);

    MidgardDriver & getMidgard()
    {
        return mMidgard;
    }

    MaliVideoDriver & getMaliVideo()
    {
        return mMaliVideo;
    }

    CCNDriver & getCcnDriver()
    {
        return mCcnDriver;
    }

    FtraceDriver & getFtraceDriver()
    {
        return mFtraceDriver;
    }

    AtraceDriver & getAtraceDriver()
    {
        return mAtraceDriver;
    }

    TtraceDriver & getTtraceDriver()
    {
        return mTtraceDriver;
    }

    ExternalDriver & getExternalDriver()
    {
        return mExternalDriver;
    }

    const PrimarySourceProvider & getPrimarySourceProvider() const
    {
        return *mPrimarySourceProvider;
    }

    PrimarySourceProvider & getPrimarySourceProvider()
    {
        return *mPrimarySourceProvider;
    }

    mali_userspace::MaliHwCntrDriver & getMaliHwCntrs()
    {
        return mMaliHwCntrs;
    }

    lib::Span<Driver * const > getAll()
    {
        return all;
    }

    lib::Span<const Driver * const > getAllConst() const
    {
        return all;
    }

    lib::Span<PolledDriver * const > getAllPolled()
    {
        return allPolled;
    }

    lib::Span<const PolledDriver * const > getAllPolledConst() const
    {
        return allPolled;
    }

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

    CLASS_DELETE_COPY_MOVE(Drivers)
    ;
};

#endif // DRIVERS_H
