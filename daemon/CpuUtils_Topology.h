/* Copyright (C) 2018-2020 by Arm Limited. All rights reserved. */

#ifndef CPU_UTILS_TOPOLOGY_H
#define CPU_UTILS_TOPOLOGY_H

#include "lib/Span.h"

#include <map>
#include <set>

namespace cpu_utils {
    void updateCpuIdsFromTopologyInformation(lib::Span<int> cpuIds,
                                             const std::map<unsigned, unsigned> & cpuToCpuIds,
                                             const std::map<unsigned, unsigned> & cpuToCluster,
                                             const std::map<unsigned, std::set<unsigned>> & clusterToCpuIds);
}

#endif // CPU_UTILS_TOPOLOGY_H
