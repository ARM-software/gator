/* Copyright (C) 2023-2024 by Arm Limited. All rights reserved. */

#pragma once
#include "lib/source_location.h"

#include <cstdint>

#include <sys/types.h>

// This file contains those items/data that are arguments to the logging functions.
// These items are needed by two different kinds of source file:
//    1. Callers of a logging function
//    2. Implementers of the logger_t, or log sinks.
// We separate these parameters out to help improve gator's build time.

namespace logging {
    /** Possible logging levels */
    enum class log_level_t : uint8_t {
        trace,
        debug,
        setup,
        fine,
        info,
        warning,
        error,
        fatal,
        child_stdout,
        child_stderr,
    };

    // the source location
    using source_loc_t = lib::source_loc_t;

    /** Timestamp (effectively just what comes from clockgettime) */
    struct log_timestamp_t {
        std::int64_t seconds;
        std::int64_t nanos;
    };

    /** Identifies the source thread */
    enum class thread_id_t : pid_t;
}
