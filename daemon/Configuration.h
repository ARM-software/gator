/**
 * Copyright (C) Arm Limited 2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CONFIGURATION_H_
#define CONFIGURATION_H_

#include <set>
#include <string>
#include <vector>

enum SampleRate
{
    high = 10007,
    normal = 1009,
    low = 101,
    none = 0,
    invalid = -1
};

enum class SpeOps
{
    LOAD,    //load
    STORE,   //store
    BRANCH   //branch
};

struct SpeConfiguration
{
    std::string id {};
    uint64_t event_filter_mask {}; // if 0 filtering is disabled, else equals PMSEVFR_EL1 (ref doc).
    std::set<SpeOps> ops {};
    int min_latency = 0;
};

static inline bool operator==(const SpeConfiguration & lhs, const SpeConfiguration & rhs) {
    return lhs.id == rhs.id;
}

static inline bool operator<(const SpeConfiguration & lhs, const SpeConfiguration & rhs) {
    return lhs.id < rhs.id;
}

namespace std
{
    template<> struct hash<SpeConfiguration>
    {
        typedef SpeConfiguration argument_type;
        typedef std::size_t result_type;
        result_type operator()(const argument_type & speConfiguration) const noexcept
        {
            return std::hash<std::string>{}(speConfiguration.id);
        }
    };
}

struct CounterConfiguration
{
    std::string counterName {};
    int event = -1;
    int count = 0;
    int cores = 0;
};

static inline bool operator==(const CounterConfiguration & lhs, const CounterConfiguration & rhs) {
    return lhs.counterName == rhs.counterName;
}

static inline bool operator<(const CounterConfiguration & lhs, const CounterConfiguration & rhs) {
    return lhs.counterName < rhs.counterName;
}

namespace std
{
    template<> struct hash<CounterConfiguration>
    {
        typedef SpeConfiguration argument_type;
        typedef std::size_t result_type;
        result_type operator()(const argument_type & counterConfiguration) const noexcept
        {
            return std::hash<std::string>{}(counterConfiguration.id);
        }
    };
}

#endif /* CONFIGURATION_H_ */
