/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include <cstdint>

/** Opaque numeric timestamp type, representing the time in nanoseconds since the capture start */
enum class monotonic_delta_t : std::uint64_t;
