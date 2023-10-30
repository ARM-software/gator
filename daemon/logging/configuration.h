/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#include "logging/logger_t.h"

#include <memory>

namespace logging {
    /**
     * Set the log sink object, which is the consumer of log messages
     *
     * @param sink Some sink object (may be null to clear the sink)
     */
    void set_logger(std::shared_ptr<logger_t> sink);

    /** Enable trace logging (which also enables debug) */
    void set_log_enable_trace(bool enabled) noexcept;
}
