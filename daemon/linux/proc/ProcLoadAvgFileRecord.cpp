/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#include "linux/proc/ProcLoadAvgFileRecord.h"

#include <cstring>
#include <cstdio>

namespace lnx
{
    namespace
    {
        static const char PROC_LOADAVG_SCANF_FORMAT[] = "%lf %lf %lf %lu/%lu %lu";
        static constexpr const int PROC_LOADAVG_SCANF_FIELD_COUNT = 6;
    }

    bool ProcLoadAvgFileRecord::parseLoadAvgFile(ProcLoadAvgFileRecord & result, const char * loadavg_contents)
    {
        if (loadavg_contents == nullptr) {
            return false;
        }

        const int nscanned = sscanf(loadavg_contents, PROC_LOADAVG_SCANF_FORMAT, &result.loadavg_1m, &result.loadavg_5m,
                                    &result.loadavg_15m, &result.num_runnable_threads, &result.num_threads,
                                    &result.newest_pid);

        return (nscanned == PROC_LOADAVG_SCANF_FIELD_COUNT);
    }

    ProcLoadAvgFileRecord::ProcLoadAvgFileRecord()
            : loadavg_1m(0),
              loadavg_5m(0),
              loadavg_15m(0),
              num_runnable_threads(0),
              num_threads(0),
              newest_pid(0)
    {
    }

    ProcLoadAvgFileRecord::ProcLoadAvgFileRecord(double loadavg_1m_, double loadavg_5m_, double loadavg_15m_,
                                                 unsigned long num_runnable_threads_, unsigned long num_threads_,
                                                 unsigned long newest_pid_)
            : loadavg_1m(loadavg_1m_),
              loadavg_5m(loadavg_5m_),
              loadavg_15m(loadavg_15m_),
              num_runnable_threads(num_runnable_threads_),
              num_threads(num_threads_),
              newest_pid(newest_pid_)
    {
    }
}
