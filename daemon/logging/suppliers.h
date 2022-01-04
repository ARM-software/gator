/* Copyright (C) 2010-2021 by Arm Limited. All rights reserved. */

#pragma once

#include <functional>
#include <string>

namespace logging {
    /** Callable that can return the last error's message (or empty string if no error) */
    using last_log_error_supplier_t = std::function<std::string()>;

    /** Callable that can return the cumulative setup messages (or empty string if no error) */
    using log_setup_supplier_t = std::function<std::string()>;
}
