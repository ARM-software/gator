/* Copyright (C) 2018-2024 by Arm Limited. All rights reserved. */

#include "CpuUtils_Topology.h"

#include "CpuUtils.h"
#include "Logging.h"
#include "lib/Span.h"

#include <cstddef>
#include <map>
#include <set>

namespace cpu_utils {
    void updateCpuIdsFromTopologyInformation(lib::Span<midr_t> midrs,
                                             const std::map<unsigned, midr_t> & cpuToMidr,
                                             const std::map<unsigned, unsigned> & cpuToCluster,
                                             const std::map<unsigned, std::set<midr_t>> & clusterToMidrs)
    {
        // update/set known items from MIDR map and topology information. This will override anything read from /proc/cpuinfo
        for (std::size_t cpu = 0; cpu < midrs.size(); ++cpu) {
            const auto cpuIdIt = cpuToMidr.find(cpu);
            if (cpuIdIt != cpuToMidr.end()) {
                // use known MIDR value
                midrs[cpu] = cpuIdIt->second;
            }
            else {
                // attempt to fill in an gaps using topology information if available
                const auto cpuClusterIt = cpuToCluster.find(cpu);
                if (cpuClusterIt != cpuToCluster.end()) {
                    const unsigned clusterId = cpuClusterIt->second;
                    const auto cpuIdsIt = clusterToMidrs.find(clusterId);
                    if (cpuIdsIt != clusterToMidrs.end()) {
                        const std::set<midr_t> & clusterMidrs = cpuIdsIt->second;
                        if (clusterMidrs.size() == 1) {
                            midrs[cpu] = *clusterMidrs.begin();
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
        for (std::size_t cpu = 0; cpu < midrs.size(); ++cpu) {
            auto const & currentMidr = midrs[cpu];

            if (currentMidr.invalid_or_other()) {
                const auto cpuClusterIt = cpuToCluster.find(cpu);
                if (cpuClusterIt != cpuToCluster.end()) {
                    const unsigned clusterId = cpuClusterIt->second;
                    const auto clusterCpusIt = clusterToCpus.find(clusterId);
                    if (clusterCpusIt != clusterToCpus.end()) {
                        const std::set<unsigned> & clusterCpus = clusterCpusIt->second;
                        std::set<midr_t> siblingMidrs;
                        for (unsigned sibling : clusterCpus) {
                            const auto & siblingMidr = midrs[sibling];
                            if ((cpu != sibling) && !siblingMidr.invalid_or_other()) {
                                siblingMidrs.insert(siblingMidr);
                            }
                        }
                        if (siblingMidrs.size() == 1) {
                            midrs[cpu] = *siblingMidrs.begin();
                        }
                    }
                }
            }
            LOG_DEBUG("CPU %zu is configured to use MIDR 0x%08x", cpu, midrs[cpu].to_raw_value());
        }
    }
}
