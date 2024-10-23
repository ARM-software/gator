/* Copyright (C) 2013-2024 by Arm Limited. All rights reserved. */

#ifndef CPU_UTILS_H
#define CPU_UTILS_H

#include "lib/Span.h"
#include "lib/midr.h"

#include <string>

namespace cpu_utils {
    unsigned int getMaxCoreNum();

    /**
     * @return hardware name if found or empty
     */
    std::string readCpuInfo(bool ignoreOffline, bool wantsHardwareName, lib::Span<midr_t> midrs);
}

#endif // CPU_UTILS_H
