/* Copyright (C) 2022-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"

#include <cstdint>
#include <ctime>

static constexpr std::uint64_t NS_PER_S = 1000000000ULL;
static constexpr std::uint64_t NS_PER_MS = 1000000ULL;
static constexpr std::uint64_t NS_PER_US = 1000ULL;

/** Opaque numeric timestamp type, representing the time in nanoseconds since the capture start */
enum class monotonic_delta_t : std::uint64_t;

#if defined(GATOR_UNIT_TESTS) && (GATOR_UNIT_TESTS != 0)
std::uint64_t getTime();
std::uint64_t getClockMonotonicTime();
#else

/** The getTime function reads the current value of CLOCK_MONOTONIC_RAW as a u64 in nanoseconds */
inline std::uint64_t getTime()
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        LOG_ERROR("Failed to get uptime");
        handleException();
    }
    return (NS_PER_S * ts.tv_sec + ts.tv_nsec);
}

inline std::uint64_t getClockMonotonicTime()
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        LOG_ERROR("Failed to get uptime");
        handleException();
    }
    return (NS_PER_S * ts.tv_sec + ts.tv_nsec);
}

#endif

/** Convert the current CLOCK_MONOTONIC_RAW to some delta from the start of the capture */
inline monotonic_delta_t monotonic_delta_now(std::uint64_t monotonic_start)
{
    return monotonic_delta_t(getTime() - monotonic_start);
}
