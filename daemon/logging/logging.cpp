/* Copyright (C) 2010-2023 by Arm Limited. All rights reserved. */

#include "Logging.h"

#include "lib/Assert.h"
#include "lib/Time.h"
#include "logging/global_log.h"

#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include <google/protobuf/stubs/logging.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace logging {
    namespace {
        std::shared_ptr<logger_t> current_logger {};

        void protobuf_log_handler(google::protobuf::LogLevel level,
                                  const char * filename,
                                  int line,
                                  const std::string & message)
        {
            auto remapped_level = log_level_t::info;
            switch (level) {
                case google::protobuf::LOGLEVEL_WARNING:
                    remapped_level = log_level_t::warning;
                    break;
                case google::protobuf::LOGLEVEL_ERROR:
                    remapped_level = log_level_t::error;
                    break;
                case google::protobuf::LOGLEVEL_FATAL:
                    remapped_level = log_level_t::fatal;
                    break; //
                default:
                    break;
            }

            log_item(remapped_level, {filename, static_cast<unsigned>(line)}, message);
        }
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
        std::shared_ptr<logger_t> sink = current_logger;

        if (sink != nullptr) {

            sink->log_item(tid, level, timestamp, location, message);
        }
    }

    void set_logger(std::shared_ptr<logger_t> sink)
    {
        current_logger = std::move(sink);

        google::protobuf::SetLogHandler(current_logger ? protobuf_log_handler : nullptr);
    }
}
