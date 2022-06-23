/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include <cstdint>

namespace apc {

    struct perf_counter_t {
        int core;
        int key;
        int64_t value;
    };

}
