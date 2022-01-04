/* Copyright (C) 2010-2021 by Arm Limited. All rights reserved. */

#pragma once

#include <cstddef>
#include <memory>
#include <string_view>

#define LOG_ITEM(level, format, ...)                                                                                   \
    ::logging::detail::do_log_item((level),                                                                            \
                                   {                                                                                   \
                                       ::logging::detail::strip_file_prefix(__FILE__),                                 \
                                       __LINE__,                                                                       \
                                   },                                                                                  \
                                   (format),                                                                           \
                                   ##__VA_ARGS__)

/** Log a 'trace' level item */
#define LOG_TRACE(format, ...)                                                                                         \
    do {                                                                                                               \
        if (::logging::is_log_enable_trace()) {                                                                        \
            LOG_ITEM(::logging::log_level_t::trace, (format), ##__VA_ARGS__);                                          \
        }                                                                                                              \
    } while (false)

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
    };

    /** Source location identifier */
    struct source_loc_t {
        std::string_view file;
        unsigned line;
    };

    /** Timestamp (effectively just what comes from clockgettime) */
    struct log_timestamp_t {
        std::int64_t seconds;
        std::int64_t nanos;
    };

    /** Log sink interface */
    class log_sink_t {
    public:
        virtual ~log_sink_t() noexcept = default;

        /** Toggle whether TRACE/DEBUG/SETUP messages are output to the console */
        virtual void set_debug_enabled(bool enabled) = 0;

        /**
         * Store some log item to the log
         *
         * @param level The log level
         * @param timestamp The timestamp of the event (CLOCK_MONOTONIC)
         * @param location The file/line source location
         * @param message The log message
         */
        virtual void log_item(log_level_t level,
                              log_timestamp_t const & timestamp,
                              source_loc_t const & location,
                              std::string_view message) = 0;
    };

    // internal helper functions used by the macros; use the macros for convenience sake
    namespace detail {
        /** Flag to enable / disable tracing, exposed here so that it can be inlined into LOG_TRACE */
        extern bool enabled_log_trace;

        /** Some compile time magic to find the last '/' in the __FILE__ path for some string constant */
        template<std::size_t N>
        constexpr std::size_t find_file_prefix_end(char const (&str)[N],
                                                   std::size_t offset = 0,
                                                   std::size_t last_found = ~std::size_t(0))
        {
            return ((str[offset] == '\0') ? (last_found != ~std::size_t(0) ? last_found                               //
                                                                           : offset)                                  //
                                          : ((str[offset] == '/') ? find_file_prefix_end(str, offset + 1, offset + 1) //
                                                                  : find_file_prefix_end(str, offset + 1, last_found)));
        }
        /** Get the length of the file path prefix for this header (as it is in the source root directory) */
        static constexpr std::size_t FILE_PREFIX_LEN = find_file_prefix_end(__FILE__);

        /** Some compile time magic to strip out the common file path prefix from some __FILE__ string as passed by one of the LOG_ITEM macros */
        template<std::size_t N>
        constexpr const char * strip_file_prefix(char const (&str)[N])
        {
            for (std::size_t i = 0; i < FILE_PREFIX_LEN; ++i) {
                if (str[i] != __FILE__[i]) {
                    return str;
                }
                if (str[i] == '\0') {
                    return str;
                }
            }
            return str + FILE_PREFIX_LEN;
        }

        /** Write out a log item */
        //NOLINTNEXTLINE(cert-dcl50-cpp)
        [[gnu::format(printf, 3, 4)]] void do_log_item(log_level_t level,
                                                       source_loc_t const & location,
                                                       const char * format,
                                                       ...);
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
     * Set the log sink object, which is the consumer of log messages
     *
     * @param sink Some sink object (may be null to clear the sink)
     */
    void set_log_sink(std::shared_ptr<log_sink_t> sink);

    /** @return true if trace logging is enabled */
    inline bool is_log_enable_trace() noexcept { return detail::enabled_log_trace; }
    /** Enable trace logging (which also enables debug) */
    inline void set_log_enable_trace(bool enabled) noexcept { detail::enabled_log_trace = enabled; }
}

extern void handleException() __attribute__((noreturn));
