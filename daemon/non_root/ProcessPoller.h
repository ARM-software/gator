/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_PROCESSPOLLER_H
#define INCLUDE_NON_ROOT_PROCESSPOLLER_H

#include "lib/FsEntry.h"
#include "lib/Optional.h"
#include "lib/TimestampSource.h"
#include "linux/proc/ProcessPollerBase.h"
#include "non_root/ProcessStateTracker.h"

namespace non_root
{
    /**
     * Scans the contents of /proc/[PID]/stat, /proc/[PID]/statm, /proc/[PID]/task/[TID]/stat and /proc/[PID]/task/[TID]/statm files
     * passing the extracted records into the ProcessStateTracker object
     */
    class ProcessPoller : private lnx::ProcessPollerBase
    {
    public:

        ProcessPoller(ProcessStateTracker & processStateTracker, lib::TimestampSource & timestampSource);
        void poll();

    private:

        ProcessStateTracker & processStateTracker;
        lib::TimestampSource & timestampSource;
    };
}

#endif /* INCLUDE_NON_ROOT_PROCESSPOLLER_H */
