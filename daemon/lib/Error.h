/* Copyright (C) 2024 by Arm Limited. All rights reserved. */

#pragma once

namespace lib {
    /// Simple wrapper for strerror, allowing control of thread-safety clang-tidy warnings
    const char * strerror(int err_no);

    /// Simple wrapper for strerror, allowing control of thread-safety clang-tidy warnings. Passes errno.
    const char * strerror();
}
