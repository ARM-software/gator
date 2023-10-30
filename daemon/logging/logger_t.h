/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#pragma once

#include "logging/parameters.h"

namespace logging {

    /** Logger interface */
    class logger_t {
    public:
        virtual ~logger_t() noexcept = default;

        /** Toggle whether TRACE/DEBUG/SETUP messages are output to the console */
        virtual void set_debug_enabled(bool enabled) = 0;

        /** Toggle whether FINE messages are output to the console */
        virtual void set_fine_enabled(bool enabled) = 0;

        /**
         * Store some log item to the log
         *
         * @param tid The originating thread ID
         * @param level The log level
         * @param timestamp The timestamp of the event (CLOCK_MONOTONIC)
         * @param location The file/line source location
         * @param message The log message
         */
        virtual void log_item(thread_id_t tid,
                              log_level_t level,
                              log_timestamp_t const & timestamp,
                              source_loc_t const & location,
                              std::string_view message) = 0;
    };
}
