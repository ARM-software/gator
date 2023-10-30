/* Copyright (C) 2023 by Arm Limited. All rights reserved. */
#pragma once

#include "lib/Span.h"

namespace agents {
    /** Agent entry point */
    int armnn_agent_main(char const * argv0, lib::Span<char const * const> args);
}
