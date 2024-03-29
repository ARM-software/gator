/* Copyright (C) 2010-2024 by Arm Limited. All rights reserved. */

#include "Logging.h"

#include "logging/configuration.h"
#include "logging/logger_t.h"
#include "logging/parameters.h"

#include <cstdarg>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string_view>
#include <utility>

#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#if __has_include(<google/protobuf/stubs/logging.h>)
#include "logging/protobuf_log_adapter.h"
#elif __has_include(<absl/log/absl_log.h>)
#include "logging/absl_log_adapter.h"
#else
#error "Protobuf logging backend could not be detected. No log handler will be installed"
#endif

namespace logging {
    namespace {
        std::shared_ptr<logger_t> current_logger {};
    }

    namespace detail {
        bool enabled_log_trace = false;

        //NOLINTNEXTLINE(cert-dcl50-cpp)
        void do_log_item(log_level_t level, source_loc_t const & location, const char * format, ...)
        {
            // format the string
            va_list varargs;
            va_start(varargs, format);
            char * buffer_ptr = nullptr;
            auto n = vasprintf(&buffer_ptr, format, varargs);
            va_end(varargs);

            if (n < 0) {
                return;
            }

            // make sure it is safely freed
            //NOLINTNEXTLINE(modernize-avoid-c-arrays)
            std::unique_ptr<char[], void (*)(void *)> buffer {buffer_ptr, std::free};

            // write it out
            log_item(level, location, std::string_view(buffer.get(), n));
        }

        void do_log_item(log_level_t level, source_loc_t const & location, std::string_view msg)
        {
            // write it out
            log_item(level, location, msg);
        }

        void do_log_item(pid_t tid, log_level_t level, source_loc_t const & location, std::string_view msg)
        {
            // write it out
            log_item(thread_id_t(tid), level, location, msg);
        }
    }

    void log_item(log_level_t level, source_loc_t const & location, std::string_view message)
    {
        std::shared_ptr<logger_t> sink = current_logger;

        if (sink != nullptr) {
            struct timespec t;
            clock_gettime(CLOCK_MONOTONIC, &t);

            sink->log_item(thread_id_t(syscall(SYS_gettid)), level, {t.tv_sec, t.tv_nsec}, location, message);
        }
    }

    void log_item(thread_id_t tid, log_level_t level, source_loc_t const & location, std::string_view message)
    {
        std::shared_ptr<logger_t> sink = current_logger;

        if (sink != nullptr) {
            struct timespec t;
            clock_gettime(CLOCK_MONOTONIC, &t);

            sink->log_item(tid, level, {t.tv_sec, t.tv_nsec}, location, message);
        }
    }

    void log_item(thread_id_t tid,
                  log_level_t level,
                  log_timestamp_t timestamp,
                  source_loc_t const & location,
                  std::string_view message)
    {
        const std::shared_ptr<logger_t> sink = current_logger;

        if (sink != nullptr) {
            sink->log_item(tid, level, timestamp, location, message);
        }
    }

    void set_logger(std::shared_ptr<logger_t> sink)
    {
        current_logger = std::move(sink);
        if (current_logger) {
            install_protobuf_log_handler();
        }
        else {
            remove_protobuf_log_handler();
        }
    }

    /** @return true if trace logging is enabled */
    bool is_log_enable_trace() noexcept
    {
        return detail::enabled_log_trace;
    }

    /** Enable trace logging (which also enables debug) */
    void set_log_enable_trace(bool enabled) noexcept
    {
        detail::enabled_log_trace = enabled;
    }

}
