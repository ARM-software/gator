/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#include "non_root/GlobalPoller.h"
#include "lib/FsEntry.h"
#include "linux/proc/ProcStatFileRecord.h"
#include "linux/proc/ProcLoadAvgFileRecord.h"

#include <string>

namespace non_root
{
    static const lib::FsEntry PROC_LOADAVG = lib::FsEntry::create("/proc/loadavg");
    static const lib::FsEntry PROC_STAT = lib::FsEntry::create("/proc/stat");

    GlobalPoller::GlobalPoller(GlobalStatsTracker & globalStateTracker_, lib::TimestampSource & timestampSource_)
            : globalStateTracker(globalStateTracker_),
              timestampSource(timestampSource_)
    {
    }

    void GlobalPoller::poll()
    {
        // do /proc/loadavg
        {
            lnx::ProcLoadAvgFileRecord loadAvgRecord;
            if (lnx::ProcLoadAvgFileRecord::parseLoadAvgFile(loadAvgRecord,
                                                             lib::readFileContents(PROC_LOADAVG).c_str())) {
                globalStateTracker.updateFromProcLoadAvgFileRecord(loadAvgRecord);
            }
        }

        // do /proc/stat
        {

            lnx::ProcStatFileRecord statRecord(lib::readFileContents(PROC_STAT).c_str());
            globalStateTracker.updateFromProcStatFileRecord(statRecord);
        }

        // send update
        globalStateTracker.sendStats(timestampSource.getTimestampNS());
    }

}
