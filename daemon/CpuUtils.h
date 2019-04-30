/**
 * Copyright (C) Arm Limited 2013-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CPU_UTILS_H
#define CPU_UTILS_H

#include <string>
#include "lib/Span.h"

namespace cpu_utils
{
    unsigned int getMaxCoreNum();

    /**
     *
     * @param cpuIds
     * @return hardware name if found or empty
     */
    std::string readCpuInfo(bool ignoreOffline, lib::Span<int> cpuIds);
}

#endif // CPU_UTILS_H
