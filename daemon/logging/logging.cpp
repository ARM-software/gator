/* Copyright (C) 2010-2022 by Arm Limited. All rights reserved. */

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
        std::shared_ptr<log_sink_t> current_log_sink {};

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
    }

    void log_item(log_level_t level, source_loc_t const & location, std::string_view message)
    {
        std::shared_ptr<log_sink_t> sink = current_log_sink;

        if (sink != nullptr) {

            struct timespec t;
            clock_gettime(CLOCK_MONOTONIC, &t);

            sink->log_item(thread_id_t(syscall(SYS_gettid)), level, {t.tv_sec, t.tv_nsec}, location, message);
        }
    }

    void log_item(thread_id_t tid,
                  log_level_t level,
                  log_timestamp_t timestamp,
                  source_loc_t const & location,
                  std::string_view message)
    {
        std::shared_ptr<log_sink_t> sink = current_log_sink;

        if (sink != nullptr) {

            sink->log_item(tid, level, timestamp, location, message);
        }
    }

    void set_log_sink(std::shared_ptr<log_sink_t> sink)
    {
        current_log_sink = std::move(sink);

        google::protobuf::SetLogHandler(current_log_sink ? protobuf_log_handler : nullptr);
    }
}
