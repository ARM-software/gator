/* Copyright (C) 2024 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "logging/parameters.h"

#include <absl/log/log_sink.h>
#include <absl/log/log_sink_registry.h>

namespace logging {

    namespace {
        class absl_log_adapter_t : public absl::LogSink {
        public:
            void Send(const absl::LogEntry & entry) override
            {
                auto remapped_level = log_level_t::info;
                switch (entry.log_severity()) {
                    case absl::LogSeverity::kWarning:
                        remapped_level = log_level_t::warning;
                        break;
                    case absl::LogSeverity::kError:
                        remapped_level = log_level_t::error;
                        break;
                    case absl::LogSeverity::kFatal:
                        remapped_level = log_level_t::fatal;
                        break; //
                    default:
                        break;
                }

                auto loc = source_loc_t({entry.source_filename().data(), entry.source_filename().length()},
                                        static_cast<unsigned>(entry.source_line()));

                std::string_view message {entry.text_message().data(), entry.text_message().length()};

                log_item(remapped_level, loc, message);
            }
        };

        inline absl_log_adapter_t adapter {};
    }

    inline void install_protobuf_log_handler()
    {
        absl::AddLogSink(&adapter);
    }

    inline void remove_protobuf_log_handler()
    {
        absl::RemoveLogSink(&adapter);
    }
}