/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#pragma once
#include <string_view>

namespace logging {
    /**
     * Log sink implementations receive formatted log messages and write them somewhere (e.g. a file/stdout).
     */
    class log_sink_t {
    public:
        log_sink_t() = default;

        virtual ~log_sink_t() = default;

        log_sink_t(const log_sink_t &) = delete;
        log_sink_t & operator=(const log_sink_t &) = delete;

        log_sink_t(log_sink_t &&) = delete;
        log_sink_t & operator=(log_sink_t &&) = delete;

        /** Emit the specified formatted log message. */
        virtual void write_log(std::string_view log_item) = 0;
    };
}
