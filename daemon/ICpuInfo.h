/* Copyright (C) 2010-2021 by Arm Limited. All rights reserved. */

#ifndef I_CPU_INFO_H
#define I_CPU_INFO_H

#include "lib/Span.h"
#include "xml/PmuXML.h"

class ICpuInfo {
public:
    virtual lib::Span<const int> getCpuIds() const = 0;

    size_t getNumberOfCores() const { return getCpuIds().size(); }

    virtual lib::Span<const GatorCpu> getClusters() const = 0;
    virtual lib::Span<const int> getClusterIds() const = 0;

    /**
     * @return null if unknown
     */
    const GatorCpu * getCluster(size_t cpu) const
    {
        const int clusterId = getClusterIds()[cpu];
        return clusterId < 0 ? nullptr : &getClusters()[clusterId];
    }

    virtual void updateIds(bool ignoreOffline) = 0;

    virtual const char * getModelName() const = 0;

    virtual ~ICpuInfo() = default;
};

#endif
