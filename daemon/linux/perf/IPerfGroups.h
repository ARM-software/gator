/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef IPERF_GROUPS_H
#define IPERF_GROUPS_H

#include <cstdint>

#include "linux/perf/PerfEventGroupIdentifier.h"

class IPerfAttrsConsumer;

class IPerfGroups
{
public:
    /// A subset of struct perf_event_attr
    struct Attr
    {
        uint32_t type = 0;
        uint64_t config = 0;
        uint64_t config1 = 0;
        uint64_t config2 = 0;
        uint64_t periodOrFreq = 0;
        uint64_t sampleType = 0;
        bool mmap = false;
        bool comm = false;
        bool freq = false;
        bool task = false;
        bool context_switch = false;
    };

    virtual bool add(uint64_t timestamp, IPerfAttrsConsumer & attrsConsumer,
                     const PerfEventGroupIdentifier & groupIdentifier, int key, const Attr & attr,
                     bool hasAuxData = false) = 0;

    virtual void addGroupLeader(const uint64_t timestamp, IPerfAttrsConsumer & attrsConsumer,
                                const PerfEventGroupIdentifier & groupIdentifier) = 0;

    virtual ~IPerfGroups() = default;

};

#endif
