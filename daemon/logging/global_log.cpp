/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#include "logging/global_log.h"

#include <iomanip>
#include <iostream>
#include <mutex>

namespace logging {
    namespace {
        inline void output_item(bool verbose,
                                char const * level,
                                log_timestamp_t const & timestamp,
                                source_loc_t const & location,
                                std::string_view const & message)
        {
            constexpr double to_ns = 1e-9;
            constexpr int pref_precision = 7;

            if (!verbose) {
                std::cerr << message << std::endl;
            }
            else {
                auto now_ns = double(timestamp.seconds) + (to_ns * double(timestamp.nanos));

                std::cerr << std::fixed << std::setprecision(pref_precision) << "[" << now_ns << "] " << level << " ("
                          << location.file << ":" << location.line << "): " << message << std::endl;
            }
        }
    }

    void global_log_sink_t::log_item(log_level_t level,
                                     log_timestamp_t const & timestamp,
                                     source_loc_t const & location,
                                     std::string_view message)
    {
        // writing to the log must be serialized in a multithreaded environment
        std::lock_guard lock {mutex};

        // special handling for certain log levels
        switch (level) {
            case log_level_t::trace:
                if (output_debug) {
                    output_item(true, "TRACE:", timestamp, location, message);
                }
                break;
            case log_level_t::debug:
                if (output_debug) {
                    output_item(true, "DEBUG:", timestamp, location, message);
                }
                break;
            case log_level_t::info:
                output_item(output_debug, "INFO: ", timestamp, location, message);
                break;
            case log_level_t::warning:
                output_item(output_debug, "WARN: ", timestamp, location, message);
                break;
            case log_level_t::setup:
                // append it to the setup log
                setup_messages.append(message).append("|");
                if (output_debug) {
                    output_item(true, "SETUP:", timestamp, location, message);
                }
                break;
            case log_level_t::error:
                // store the last error message
                last_error = std::string(message);
                output_item(output_debug, "ERROR:", timestamp, location, message);
                break;
            case log_level_t::fatal:
                // store the last error message
                last_error = std::string(message);
                output_item(output_debug, "FATAL:", timestamp, location, message);
                break;
        }
    }
}
