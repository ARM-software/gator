/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef CPU_UTILS_H
#define CPU_UTILS_H

#include "lib/Span.h"

#include <string>

namespace cpu_utils {
    unsigned int getMaxCoreNum();

    /**
     *
     * @param cpuIds
     * @return hardware name if found or empty
     */
    std::string readCpuInfo(bool ignoreOffline, lib::Span<int> cpuIds);
}

#endif // CPU_UTILS_H
