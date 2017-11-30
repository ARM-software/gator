/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#include "linux/proc/ProcPidStatmFileRecord.h"

#include <cstring>
#include <cstdio>

namespace lnx
{
    namespace
    {
        static const char PROC_STAT_SCANF_FORMAT[] = "%lu %lu %lu %lu %lu %lu %lu";
        static constexpr const int PROC_STAT_SCANF_FIELD_COUNT = 7;
    }

    bool ProcPidStatmFileRecord::parseStatmFile(ProcPidStatmFileRecord & result, const char * statm_contents)
    {
        if (statm_contents == nullptr) {
            return false;
        }

        const int nscanned = sscanf(statm_contents, PROC_STAT_SCANF_FORMAT, &result.size, &result.resident,
                                    &result.shared, &result.text, &result.lib, &result.data, &result.dt);

        return (nscanned == PROC_STAT_SCANF_FIELD_COUNT);
    }

    ProcPidStatmFileRecord::ProcPidStatmFileRecord()
            : size(0),
              resident(0),
              shared(0),
              text(0),
              lib(0),
              data(0),
              dt(0)
    {
    }

    ProcPidStatmFileRecord::ProcPidStatmFileRecord(unsigned long size_, unsigned long resident_, unsigned long shared_,
                                           unsigned long text_, unsigned long lib_, unsigned long data_, unsigned long dt_)
            : size(size_),
              resident(resident_),
              shared(shared_),
              text(text_),
              lib(lib_),
              data(data_),
              dt(dt_)
    {
    }
}
