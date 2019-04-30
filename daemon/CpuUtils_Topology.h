/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#ifndef CPU_UTILS_TOPOLOGY_H
#define CPU_UTILS_TOPOLOGY_H

#include <map>
#include <set>

#include "lib/Span.h"

namespace cpu_utils
{
    void updateCpuIdsFromTopologyInformation(lib::Span<int> cpuIds,
                                             const std::map<unsigned, unsigned> & cpuToCpuIds,
                                             const std::map<unsigned, unsigned> & cpuToCluster,
                                             const std::map<unsigned, std::set<unsigned>> & clusterToCpuIds);
}

#endif // CPU_UTILS_TOPOLOGY_H
