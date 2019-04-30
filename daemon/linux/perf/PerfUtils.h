/**
 * Copyright (C) Arm Limited 2010-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PERF_UTILS_H
#define PERF_UTILS_H

#include <set>
#include <string>

#include "lib/Format.h"
#include "lib/Utils.h"

namespace perf_utils
{
    static inline std::set<int> readCpuMask(const char * pmncName)
    {
        std::string path = lib::Format() << "/sys/bus/event_source/devices/" << pmncName << "/cpumask";
        return lib::readCpuMaskFromFile(path.c_str());
    }
}

#endif // PERF_UTILS_H
