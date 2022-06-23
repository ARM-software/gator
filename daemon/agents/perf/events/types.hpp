/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include <cstdint>

namespace agents::perf {
    enum class core_no_t : int;

    enum class gator_key_t : int {
        magic_key_timestamp = 0,
        magic_key_tid = 1,
        magic_key_core = 2,
        first_free_key = 4,
    };

    enum class cpu_cluster_id_t : int {
        invalid = -1,
    };

    enum class uncore_pmu_id_t : int {
        invalid = -1,
    };

    enum class perf_event_id_t : std::uint64_t {
        invalid = 0,
    };
}
