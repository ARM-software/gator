/* Copyright (C) 2010-2023 by Arm Limited. All rights reserved. */

#pragma once

#include "Config.h"
#include "lib/Span.h"
#include "lib/source_location.h"

#include <cstddef>
#include <memory>
#include <string_view>

#define LOG_ITEM(level, format, ...)                                                                                   \
    ::logging::detail::do_log_item((level), lib::source_loc_t {__FILE__, __LINE__}, (format), ##__VA_ARGS__)

#if CONFIG_LOG_TRACE
/** Log a 'trace' level item */
#define LOG_TRACE(format, ...)                                                                                         \
    do {                                                                                                               \
        if (::logging::is_log_enable_trace()) {                                                                        \
            LOG_ITEM(::logging::log_level_t::trace, (format), ##__VA_ARGS__);                                          \
        }                                                                                                              \
    } while (false)
#else
/** ignore LOG_TRACE */
template<typename... Args>
inline void LOG_TRACE(char const *, Args &&...)
{
}
#endif

/** Log a 'debug' level item */
#define LOG_DEBUG(format, ...) LOG_ITEM(::logging::log_level_t::debug, (format), ##__VA_ARGS__)

/** Log a 'info' level item */
#define LOG_INFO(format, ...) LOG_ITEM(::logging::log_level_t::info, (format), ##__VA_ARGS__)

/** Log a 'setup' level item */
#define LOG_SETUP(format, ...) LOG_ITEM(::logging::log_level_t::setup, (format), ##__VA_ARGS__)

/** Log a 'warning' level item */
#define LOG_WARNING(format, ...) LOG_ITEM(::logging::log_level_t::warning, (format), ##__VA_ARGS__)

/** Log a 'error' level item */
#define LOG_ERROR(format, ...) LOG_ITEM(::logging::log_level_t::error, (format), ##__VA_ARGS__)

/** Log a 'fatal' level item */
#define LOG_FATAL(format, ...) LOG_ITEM(::logging::log_level_t::fatal, (format), ##__VA_ARGS__)

/** Log a 'child stdout' level item */
#define LOG_STDOUT(tid, text)                                                                                          \
    ::logging::detail::do_log_item((tid),                                                                              \
                                   ::logging::log_level_t::child_stdout,                                               \
                                   lib::source_loc_t {__FILE__, __LINE__},                                             \
                                   (text))

/** Log a 'child stderr' level item */
#define LOG_STDERR(tid, text)                                                                                          \
    ::logging::detail::do_log_item((tid),                                                                              \
                                   ::logging::log_level_t::child_stderr,                                               \
                                   lib::source_loc_t {__FILE__, __LINE__},                                             \
                                   (text))

/** Log an 'error' if the value of ec is not EOF */
#define LOG_ERROR_IF_NOT_EOF(ec, format, ...)                                                                          \
    do {                                                                                                               \
        if ((ec) != boost::asio::error::eof) {                                                                         \
            LOG_ERROR((format), ##__VA_ARGS__);                                                                        \
        }                                                                                                              \
    } while (false)

#define LOG_ERROR_IF_NOT_EOF_OR_CANCELLED(ec, format, ...)                                                             \
    do {                                                                                                               \
        if (((ec) != boost::asio::error::eof) && ((ec) != boost::asio::error::operation_aborted)) {                    \
            LOG_ERROR((format), ##__VA_ARGS__);                                                                        \
        }                                                                                                              \
    } while (false)

namespace logging {
    /** Possible logging levels */
    enum class log_level_t {
        trace,
        debug,
        setup,
        info,
        warning,
        error,
        fatal,
        child_stdout,
        child_stderr,
    };

    // the source location
    using source_loc_t = lib::source_loc_t;

    /** Timestamp (effectively just what comes from clockgettime) */
    struct log_timestamp_t {
        std::int64_t seconds;
        std::int64_t nanos;
    };

    /** Identifies the source thread */
    enum class thread_id_t : pid_t;

    /** Log sink interface */
    class log_sink_t {
    public:
        virtual ~log_sink_t() noexcept = default;

        /** Toggle whether TRACE/DEBUG/SETUP messages are output to the console */
        virtual void set_debug_enabled(bool enabled) = 0;

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

    // internal helper functions used by the macros; use the macros for convenience sake
    namespace detail {
        /** Flag to enable / disable tracing, exposed here so that it can be inlined into LOG_TRACE */
        extern bool enabled_log_trace;

        /** Write out a log item */
        //NOLINTNEXTLINE(cert-dcl50-cpp)
        [[gnu::format(printf, 3, 4)]] void do_log_item(log_level_t level,
                                                       source_loc_t const & location,
                                                       const char * format,
                                                       ...);

        /** Write out a log item */
        void do_log_item(log_level_t level, source_loc_t const & location, std::string_view msg);

        /** Write out a log item */
        void do_log_item(pid_t tid, log_level_t level, source_loc_t const & location, std::string_view msg);
    }

    /**
     * Store some log item to the log
     *
     * @param level The log level
     * @param location The file/line source location
     * @param message The log message
     */
    void log_item(log_level_t level, source_loc_t const & location, std::string_view message);

    /**
     * Store some log item to the log
     *
     * @param tid The originating thread ID
     * @param level The log level
     * @param location The file/line source location
     * @param message The log message
     */
    void log_item(thread_id_t tid, log_level_t level, source_loc_t const & location, std::string_view message);

    /**
     * Store some log item to the log
     *
     * @param tid The originating thread ID
     * @param level The log level
     * @param timestamp The log timestamp
     * @param location The file/line source location
     * @param message The log message
     */
    void log_item(thread_id_t tid,
                  log_level_t level,
                  log_timestamp_t timestamp,
                  source_loc_t const & location,
                  std::string_view message);

    /**
     * Set the log sink object, which is the consumer of log messages
     *
     * @param sink Some sink object (may be null to clear the sink)
     */
    void set_log_sink(std::shared_ptr<log_sink_t> sink);

    /** @return true if trace logging is enabled */
    inline bool is_log_enable_trace() noexcept
    {
        return detail::enabled_log_trace;
    }
    /** Enable trace logging (which also enables debug) */
    inline void set_log_enable_trace(bool enabled) noexcept
    {
        detail::enabled_log_trace = enabled;
    }
    /** Enable trace logging based on the --trace argument on the command line */
    inline void set_log_enable_trace(lib::Span<char const * const> argv)
    {
        for (auto const * arg : argv) {
            if (std::string_view(arg) == "--trace") {
                set_log_enable_trace(true);
                return;
            }
        }
    }
}

extern void handleException() __attribute__((noreturn));
