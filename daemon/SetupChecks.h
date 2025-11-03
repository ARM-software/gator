/* Copyright (C) 2025 by Arm Limited. All rights reserved. */
#pragma once

#include "setup_warnings.h"

#include <string_view>

inline constexpr std::string_view spe_not_enabled_error =
    "SPE requested but the Arm SPE driver was not detected on this machine.\n"
    "It may be possible to enable SPE support by loading the appropriate driver "
    "using `modprobe arm_spe_pmu`\n"
    "SPE is generally not available in virtualized environments or when the SPE "
    "hardware is not exposed correctly by firmware.";

inline constexpr std::string_view spe_not_supported_error =
    "SPE requested but the Arm SPE driver was not detected on this machine. \n"
    "The CPU is not known to support SPE.";

[[nodiscard]] bool check_spe_available(setup_warnings_t &, lib::Span<GatorCpu const>);
