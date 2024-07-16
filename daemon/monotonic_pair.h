/* Copyright (C) 2024 by Arm Limited. All rights reserved. */

#pragma once

#include <cstdint>

struct [[gnu::packed]] monotonic_pair_t {
    std::uint64_t monotonic_raw;
    std::uint64_t monotonic;

    friend bool operator==(monotonic_pair_t const & a, monotonic_pair_t const & b)
    {
        return (a.monotonic_raw == b.monotonic_raw && a.monotonic == b.monotonic);
    }
    friend bool operator!=(monotonic_pair_t const & a, monotonic_pair_t const & b) { return !(a == b); }
};
