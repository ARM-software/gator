/* Copyright (C) 2018-2024 by Arm Limited. All rights reserved. */

#ifndef CPU_UTILS_TOPOLOGY_H
#define CPU_UTILS_TOPOLOGY_H

#include "lib/Span.h"
#include "lib/midr.h"

#include <map>
#include <set>

namespace cpu_utils {
    void updateCpuIdsFromTopologyInformation(lib::Span<midr_t> midrs,
                                             const std::map<unsigned, midr_t> & cpuToMidr,
                                             const std::map<unsigned, unsigned> & cpuToCluster,
                                             const std::map<unsigned, std::set<midr_t>> & clusterToMidrs);
}

#endif // CPU_UTILS_TOPOLOGY_H
