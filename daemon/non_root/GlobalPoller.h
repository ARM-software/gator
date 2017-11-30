/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_GLOBALPOLLER_H
#define INCLUDE_NON_ROOT_GLOBALPOLLER_H

#include "lib/TimestampSource.h"
#include "non_root/GlobalStatsTracker.h"

namespace non_root
{
    /**
     * Scans the contents of /proc/stat and /proc/loadavg passing the extracted records into the GlobalStatsTracker object
     */
    class GlobalPoller
    {
    public:

        GlobalPoller(GlobalStatsTracker & globalStateTracker, lib::TimestampSource & timestampSource);

        void poll();

    private:

        GlobalStatsTracker & globalStateTracker;
        lib::TimestampSource & timestampSource;
    };
}

#endif /* INCLUDE_NON_ROOT_GLOBALPOLLER_H */
