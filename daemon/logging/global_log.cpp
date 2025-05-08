/* Copyright (C) 2021-2024 by Arm Limited. All rights reserved. */

#include "logging/global_log.h"

#include "file_log_sink.h"
#include "logging/parameters.h"
#include "logging/suppliers.h"

#include <array>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>

#include <sched.h>

namespace {
    constexpr auto log_Level_string =
        std::array<std::string_view,
                   10> {"TRACE", "DEBUG", "SETUP", "FINE", "INFO", "WARN", "ERROR", "FATAL", "STDOU", "STDER"};
}

namespace logging {

    global_logger_t::global_logger_t()
    {
        // disable buffering of output
        (void) ::setvbuf(stdout, nullptr, _IONBF, 0);
        (void) ::setvbuf(stderr, nullptr, _IONBF, 0);
        // make sure that everything goes to output immediately
        std::cout << std::unitbuf;
        std::cerr << std::unitbuf;
    }

    void global_logger_t::log_item(thread_id_t tid,
                                   log_level_t level,
                                   log_timestamp_t const & timestamp,
                                   source_loc_t const & location,
                                   std::string_view message)
    {
        // writing to the log must be serialized in a multithreaded environment
        const std::lock_guard lock {mutex};

        const bool verbose_log = output_debug || output_fine;

        // special handling for certain log levels
        switch (level) {
            case log_level_t::trace:
            case log_level_t::debug:
                if (output_debug) {
                    output_item(true, level, tid, timestamp, location, message);
                }
                break;
            case log_level_t::fine:
                if (output_fine || output_debug) {
                    output_item(true, level, tid, timestamp, location, message);
                }
                break;
            case log_level_t::info:
            case log_level_t::warning:
                output_item(verbose_log, level, tid, timestamp, location, message);
                break;
            case log_level_t::setup:
                // append it to the setup log
                setup_messages.append(message).append("|");
                if (output_debug) {
                    output_item(true, level, tid, timestamp, location, message);
                }
                break;
            case log_level_t::error:
            case log_level_t::fatal:
                // store the last error message
                last_error = std::string(message);
                output_item(verbose_log, level, tid, timestamp, location, message);
                break;
            case log_level_t::child_stdout:
                if (output_debug) {
                    output_item(verbose_log, level, tid, timestamp, location, message);
                }
                // always output to cout, regardless of whether the cerr log was also output
                std::cout << message;
                break;
            case log_level_t::child_stderr:
                if (output_debug) {
                    output_item(verbose_log, level, tid, timestamp, location, message);
                }
                else {
                    std::cerr << message;
                }
                break;
        }
    }

    void global_logger_t::output_item(bool verbose,
                                      log_level_t level,
                                      thread_id_t tid,
                                      log_timestamp_t const & timestamp,
                                      source_loc_t const & location,
                                      std::string_view const & message)
    {
        constexpr double to_ns = 1e-9;
        constexpr int pref_precision = 7;

        if (!verbose) {
            if (level == log_level_t::setup || level == log_level_t::info || level == log_level_t::child_stdout
                || level == log_level_t::child_stderr) {
                for (auto & sink : sinks) {
                    sink->write_log(message);
                }
            }
            else {
                format_buffer.str({});
                format_buffer << log_Level_string[static_cast<int>(level)] << ": " << message;
                auto str = format_buffer.str();
                for (auto & sink : sinks) {
                    sink->write_log(str);
                }
            }
        }
        else {
            auto now_ns = double(timestamp.seconds) + (to_ns * double(timestamp.nanos));
            format_buffer.str({});

            format_buffer << std::fixed << std::setprecision(pref_precision) << "[" << now_ns << "] "
                          << log_Level_string[static_cast<int>(level)] << ": #" << pid_t(tid) << " ("
                          << location.file_name() << ":" << location.line_no() << "): " << message;

            auto str = format_buffer.str();
            for (auto & sink : sinks) {
                sink->write_log(str);
            }
        }
    }

    log_file_t global_logger_t::capture_log_file() const
    {
        if (file_sink == nullptr) {
            return {};
        }
        return {file_sink->get_log_file_path()};
    }

    void global_logger_t::restart_log_file()
    {
        if (file_sink != nullptr) {
            file_sink->restart_log_file();
        }
    }
}
