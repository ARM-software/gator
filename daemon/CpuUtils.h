/* Copyright (C) 2013-2025 by Arm Limited. All rights reserved. */

#ifndef CPU_UTILS_H
#define CPU_UTILS_H

#include "lib/Span.h"
#include "lib/midr.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace cpu_utils {
    struct topology_info_t {
        using core_idx = std::uint32_t;
        using cluster_idx = std::uint32_t;

        std::map<core_idx, midr_t> cpu_to_midr;
        std::map<core_idx, cluster_idx> cpu_to_cluster;
        std::map<cluster_idx, std::set<midr_t>> cluster_to_midrs;
    };

    unsigned int getMaxCoreNum();

    topology_info_t read_cpu_topology(bool ignore_offline, std::size_t max_cpu_number);

    /**
     * @return hardware name if found or empty
     */
    std::string readCpuInfo(bool ignoreOffline, bool wantsHardwareName, lib::Span<midr_t> midrs);
}

#endif // CPU_UTILS_H
