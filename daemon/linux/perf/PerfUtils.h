/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#ifndef PERF_UTILS_H
#define PERF_UTILS_H

#include "lib/Format.h"
#include "lib/Optional.h"
#include "lib/Utils.h"

#include <set>
#include <string>

namespace perf_utils {
    inline std::set<int> readCpuMask(const char * pmncName)
    {
        std::string path = lib::Format() << "/sys/bus/event_source/devices/" << pmncName << "/cpumask";
        return lib::readCpuMaskFromFile(path.c_str());
    }

    inline lib::Optional<std::int64_t> readPerfEventMlockKb()
    {
        std::int64_t perfEventMlockKb = 0;

        if (lib::readInt64FromFile("/proc/sys/kernel/perf_event_mlock_kb", perfEventMlockKb) == 0) {
            return lib::Optional<std::int64_t>(perfEventMlockKb);
        }
        else {
            return lib::Optional<std::int64_t>();
        }
    }
}

#endif // PERF_UTILS_H
