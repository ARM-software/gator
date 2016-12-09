/**
 * Copyright (C) ARM Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERDRIVER_H_
#define NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERDRIVER_H_

#include "SimpleDriver.h"
#include "mali_userspace/MaliHwCntrReader.h"

namespace mali_userspace
{
    /**
     * Implements a counter driver for all Mali Midgard devices with r8p0 or later driver installed.
     * Allows reading counters from userspace, without modification to mali driver, or installation of gator.ko
     */
    class MaliHwCntrDriver : public SimpleDriver
    {
        typedef SimpleDriver super;

    public:

        MaliHwCntrDriver();
        ~MaliHwCntrDriver();

        bool claimCounter(const Counter &counter) const;
        void resetCounters();
        void setupCounter(Counter &counter);
        bool start();

        inline MaliHwCntrReader * getReader()
        {
            return mReader;
        }

        inline const MaliHwCntrReader * getReader() const
        {
            return mReader;
        }

        int getCounterKey(uint32_t nameBlockIndex, uint32_t counterIndex) const;

        const char * getSupportedDeviceFamilyName() const;

    private:

        /** The reader object */
        MaliHwCntrReader * mReader;
        /** For each possible counter index, contains the counter key, or 0 if not enabled */
        int * mEnabledCounterKeys;

        bool query();

        // Intentionally unimplemented
        MaliHwCntrDriver(const MaliHwCntrDriver &);
        MaliHwCntrDriver &operator=(const MaliHwCntrDriver &);
    };
}

#endif // NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERDRIVER_H_
