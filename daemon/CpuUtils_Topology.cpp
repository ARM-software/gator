/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#include "CpuUtils_Topology.h"
#include "Logging.h"

namespace cpu_utils
{
    void updateCpuIdsFromTopologyInformation(lib::Span<int> cpuIds,
                                             const std::map<unsigned, unsigned> & cpuToCpuIds,
                                             const std::map<unsigned, unsigned> & cpuToCluster,
                                             const std::map<unsigned, std::set<unsigned>> & clusterToCpuIds)
    {
        // update/set known items from MIDR map and topology information. This will override anything read from /proc/cpuinfo
        for (unsigned cpu = 0; cpu < cpuIds.length; ++cpu) {
            const auto cpuIdIt = cpuToCpuIds.find(cpu);
            if (cpuIdIt != cpuToCpuIds.end()) {
                // use known MIDR value
                cpuIds[cpu] = cpuIdIt->second;
            }
            else {
                // attempt to fill in an gaps using topology information if available
                const auto cpuClusterIt = cpuToCluster.find(cpu);
                if (cpuClusterIt != cpuToCluster.end()) {
                    const unsigned clusterId = cpuClusterIt->second;
                    const auto cpuIdsIt = clusterToCpuIds.find(clusterId);
                    if (cpuIdsIt != clusterToCpuIds.end()) {
                        const std::set<unsigned> & clusterCpuIds = cpuIdsIt->second;
                        if (clusterCpuIds.size() == 1) {
                            const unsigned cpuId = *clusterCpuIds.begin();
                            cpuIds[cpu] = cpuId;
                        }
                    }
                }
            }
        }

        // build reverse lookup for cluster to cpu for pass two
        std::map<unsigned, std::set<unsigned>> clusterToCpus;
        for (auto pair : cpuToCluster) {
            clusterToCpus[pair.second].insert(pair.first);
        }

        // second pass, try to fill gaps based on siblings
        for (unsigned cpu = 0; cpu < cpuIds.length; ++cpu) {
            const unsigned currentCpuId = cpuIds[cpu] & 0xfffff;
            if ((currentCpuId == 0) || (currentCpuId == 0xfffff)) {
                const auto cpuClusterIt = cpuToCluster.find(cpu);
                if (cpuClusterIt != cpuToCluster.end()) {
                    const unsigned clusterId = cpuClusterIt->second;
                    const auto clusterCpusIt = clusterToCpus.find(clusterId);
                    if (clusterCpusIt != clusterToCpus.end()) {
                        const std::set<unsigned> & clusterCpus = clusterCpusIt->second;
                        std::set<unsigned> siblingCpuIds;
                        for (unsigned sibling : clusterCpus) {
                            const unsigned siblingCpuId = cpuIds[sibling] & 0xfffff;
                            if ((cpu != sibling) && (siblingCpuId != 0) && (siblingCpuId != 0xfffff)) {
                                siblingCpuIds.insert(siblingCpuId);
                            }
                        }
                        if (siblingCpuIds.size() == 1) {
                            const unsigned cpuId = *siblingCpuIds.begin();
                            cpuIds[cpu] = cpuId;
                        }
                    }
                }
            }
            logg.logMessage("CPU %u is configured to use CPUID 0x%05x", cpu, cpuIds[cpu]);
        }
    }
}



