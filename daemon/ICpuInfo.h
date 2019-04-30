/**
 * Copyright (C) Arm Limited 2010-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef I_CPU_INFO_H
#define I_CPU_INFO_H

#include "lib/Span.h"
#include "PmuXML.h"

class ICpuInfo
{
public:

    virtual lib::Span<const int> getCpuIds() const = 0;

    size_t getNumberOfCores() const
    {
        return getCpuIds().size();
    }

    virtual lib::Span<const GatorCpu> getClusters() const = 0;
    virtual lib::Span<const int> getClusterIds() const = 0;

    /**
     *
     * @param cpu
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
