/* Copyright (C) 2024 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"

#include <google/protobuf/stubs/logging.h>

namespace logging {

    namespace {
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

    inline void install_protobuf_log_handler()
    {
        google::protobuf::SetLogHandler(protobuf_log_handler);
    }

    inline void remove_protobuf_log_handler()
    {
        google::protobuf::SetLogHandler(nullptr);
    }
}