/* Copyright (C) 2021 by Arm Limited. All rights reserved. */
#pragma once

#include "lib/Span.h"

namespace agents {
    /** Agent entry point */
    int ext_agent_main(char const * argv0, lib::Span<char const * const> args);
}
