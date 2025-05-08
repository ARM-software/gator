/* Copyright (C) 2010-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "logging/file_log_sink.h"
#include "logging/logger_t.h"
#include "logging/suppliers.h"

#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace logging {

    /** Default log sink, prints to stdout / stderr depending on message type and configuration */
    class global_logger_t : public logger_t, public log_access_ops_t {
    public:
        global_logger_t();

        /** Toggle whether TRACE/DEBUG/SETUP messages are output to the console */
        void set_debug_enabled(bool enabled) override { output_debug = enabled; }
        /** Toggle whether FINE messages are output to the console */
        void set_fine_enabled(bool enabled) override { output_fine = enabled; }
        /** Store some log item to the log */
        void log_item(thread_id_t tid,
                      log_level_t level,
                      log_timestamp_t const & timestamp,
                      source_loc_t const & location,
                      std::string_view message) override;

        /** Access the last sent error log item */
        [[nodiscard]] std::string get_last_log_error() const override
        {
            const std::lock_guard<std::mutex> lock {mutex};
            return last_error;
        }
        /** Access the acumulation of all setup messages */
        [[nodiscard]] std::string get_log_setup_messages() const override
        {
            const std::lock_guard<std::mutex> lock {mutex};
            return setup_messages;
        }

        template<typename Logger,
                 typename... Args,
                 std::enable_if_t<!std::is_same_v<Logger, file_log_sink_t>, bool> = false>
        void add_sink(Args &&... args)
        {
            sinks.push_back(std::make_unique<Logger>(std::forward<Args>(args)...));
        }

        // this template jank is to work around a bug in older versions of GCC that breaks full specialisation of
        // member function templates
        template<typename Logger, std::enable_if_t<std::is_same_v<Logger, file_log_sink_t>, bool> = false>
        void add_sink()
        {
            // don't create a second file logger if we already have one
            if (file_sink != nullptr) {
                return;
            }

            auto sink = std::make_unique<file_log_sink_t>();
            file_sink = sink.get();
            sinks.push_back(std::move(sink));
        }

        [[nodiscard]] log_file_t capture_log_file() const override;

        void restart_log_file() override;

    private:
        /** To protect against concurrect modifications */
        mutable std::mutex mutex {};

        /** The last seen error message */
        std::string last_error {};

        /** The setup log buffer */
        std::string setup_messages {};

        /** Is debug (and setup) level enabled for output */
        bool output_debug = false;

        /** Is fine level enabled for output */
        bool output_fine = false;

        /** The buffer used to format a verbose log message before sending to the sinks */
        std::stringstream format_buffer {};

        /** The list of sinks to send formatted log messages to */
        std::vector<std::unique_ptr<log_sink_t>> sinks {};

        /** Pointer to the file sink, if there is one, so that we can ask it to do things with the file. */
        file_log_sink_t * file_sink {};

        /** Formats a long message and sends it to the sinks */
        void output_item(bool verbose,
                         log_level_t level,
                         thread_id_t tid,
                         log_timestamp_t const & timestamp,
                         source_loc_t const & location,
                         std::string_view const & message);
    };
}
