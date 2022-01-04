/* Copyright (C) 2010-2021 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"

#include <mutex>
#include <string>
#include <string_view>

namespace logging {
    /** Default log sink, prints to stdout / stderr depending on message type and configuration */
    class global_log_sink_t : public log_sink_t {
    public:
        /** Toggle whether TRACE/DEBUG/SETUP messages are output to the console */
        void set_debug_enabled(bool enabled) override { output_debug = enabled; }
        /** Store some log item to the log */
        void log_item(log_level_t level,
                      log_timestamp_t const & timestamp,
                      source_loc_t const & location,
                      std::string_view message) override;

        /** Access the last sent error log item */
        [[nodiscard]] std::string get_last_log_error() const
        {
            std::lock_guard<std::mutex> lock {mutex};
            return last_error;
        }
        /** Access the acumulation of all setup messages */
        [[nodiscard]] std::string get_log_setup_messages() const
        {
            std::lock_guard<std::mutex> lock {mutex};
            return setup_messages;
        }

    private:
        /** To protect against concurrect modifications */
        mutable std::mutex mutex {};

        /** The last seen error message */
        std::string last_error {};

        /** The setup log buffer */
        std::string setup_messages {};

        /** Is debug (and setup) level enabled for output */
        bool output_debug = false;
    };
}
