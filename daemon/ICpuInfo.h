/* Copyright (C) 2010-2024 by Arm Limited. All rights reserved. */

#ifndef I_CPU_INFO_H
#define I_CPU_INFO_H

#include "lib/Span.h"
#include "lib/midr.h"
#include "xml/PmuXML.h"

class ICpuInfo {
public:
    virtual ~ICpuInfo() = default;

    [[nodiscard]] size_t getNumberOfCores() const { return getMidrs().size(); }

    [[nodiscard]] virtual lib::Span<const cpu_utils::midr_t> getMidrs() const = 0;
    [[nodiscard]] virtual lib::Span<const GatorCpu> getClusters() const = 0;
    [[nodiscard]] virtual lib::Span<const int> getClusterIds() const = 0;
    [[nodiscard]] virtual const char * getModelName() const = 0;

    virtual void updateIds(bool ignoreOffline) = 0;

    /** @return null if unknown */
    [[nodiscard]] const GatorCpu * getCluster(size_t cpu) const
    {
        const int clusterId = getClusterIds()[cpu];
        return clusterId < 0 ? nullptr : &getClusters()[clusterId];
    }

protected:
    static void updateClusterIds(lib::Span<const cpu_utils::midr_t> midrs,
                                 lib::Span<const GatorCpu> clusters,
                                 lib::Span<int> cluserIds)
    {
        int lastClusterId = 0;
        for (size_t i = 0; i < midrs.size(); ++i) {
            int clusterId = -1;
            for (size_t j = 0; j < clusters.size(); ++j) {
                if (clusters[j].hasCpuId(midrs[i].to_cpuid())) {
                    clusterId = j;
                }
            }
            if (clusterId == -1) {
                // No corresponding cluster found for this CPU, most likely this is a big LITTLE system without multi-PMU support
                // assume it belongs to the last cluster seen
                cluserIds[i] = lastClusterId;
            }
            else {
                cluserIds[i] = clusterId;
                lastClusterId = clusterId;
            }
        }
    }
};

#endif
