/* Copyright (C) 2013-2024 by Arm Limited. All rights reserved. */

#ifndef IPERF_GROUPS_H
#define IPERF_GROUPS_H

#include "linux/perf/PerfEventGroupIdentifier.h"
#include "linux/perf/attr_to_key_mapping_tracker.h"

#include <cstdint>

class IPerfAttrsConsumer;

class IPerfGroups {
public:
    /// A subset of struct perf_event_attr
    struct Attr {
        std::uint32_t type = 0;
        std::uint64_t config = 0;
        std::uint64_t config1 = 0;
        std::uint64_t config2 = 0;
        std::uint64_t periodOrFreq = 0;
        std::uint64_t strobePeriod = 0;
        std::uint64_t sampleType = 0;
        bool mmap = false;
        bool comm = false;
        bool freq = false;
        bool task = false;
        bool context_switch = false;
        bool userspace_only = false;

        constexpr Attr() = default;
    };

    [[nodiscard]] virtual int nextDummyKey() = 0;

    [[nodiscard]] virtual bool add(attr_to_key_mapping_tracker_t & mapping_tracker,
                                   const PerfEventGroupIdentifier & groupIdentifier,
                                   int key,
                                   const Attr & attr,
                                   bool hasAuxData = false) = 0;

    virtual void addGroupLeader(attr_to_key_mapping_tracker_t & mapping_tracker,
                                const PerfEventGroupIdentifier & groupIdentifier) = 0;

    virtual ~IPerfGroups() = default;
};

#endif
