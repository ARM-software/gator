/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include <lib/Span.h>

namespace agents::perf {

    /**
     * Perf agent entry point.
     */
    int perf_agent_main(char const * argv0, lib::Span<const char * const> args);
}
