/* Copyright (C) 2022-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "linux/perf/PerfConfig.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace agents::perf {

    struct perf_driver_summary_state_t {
        std::map<std::string, std::string> additional_attributes;
        std::string uname;
        std::uint64_t clock_realtime;
        std::uint64_t clock_boottime;
        std::uint64_t clock_monotonic_raw;
        std::uint64_t clock_monotonic;
        long page_size;
        bool nosync;
    };

    std::optional<perf_driver_summary_state_t> create_perf_driver_summary_state(PerfConfig const & perf_config,
                                                                                std::uint64_t clock_monotonic_raw,
                                                                                std::uint64_t clock_monotonic,
                                                                                bool system_wide);
}
