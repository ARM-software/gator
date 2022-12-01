/* Copyright (C) 2022 by Arm Limited. All rights reserved. */
#pragma once

#include "Logging.h"
#include "agents/agent_environment.h"
#include "agents/common/socket_listener.h"
#include "agents/common/socket_reference.h"
#include "agents/common/socket_worker.h"
#include "agents/ext_source/ipc_sink_wrapper.h"
#include "agents/perfetto/perfetto_sdk_helper.h"
#include "android/PropertyUtils.h"
#include "async/completion_handler.h"
#include "async/continuations/continuation.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "ipc/messages.h"
#include "ipc/raw_ipc_channel_sink.h"
#include "ipc/raw_ipc_channel_source.h"
#include "lib/Utils.h"

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

#include <boost/asio/io_context.hpp>

namespace agents {
    /**
     * The main agent object for the perfetto agent
     */
    template<typename PerfettoSdkHelper = agents::perfetto_sdk_helper_t>
    class perfetto_agent_t : public std::enable_shared_from_this<perfetto_agent_t<PerfettoSdkHelper>> {
        static constexpr std::size_t buffer_sz = 4096;
        static constexpr std::array<char, 11> protocol_handshake_tag =
            {'P', 'E', 'R', 'F', 'E', 'T', 'T', 'O', '\n', '\x0A', '\0'};

    public:
        using accepted_message_types = std::tuple<ipc::msg_perfetto_close_conn_t, ipc::msg_monitored_pids_t>;

        using perfetto_sdk_helper_t = PerfettoSdkHelper;

        static std::shared_ptr<perfetto_agent_t<perfetto_sdk_helper_t>> create(
            boost::asio::io_context & io_context,
            std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink,
            agent_environment_base_t::terminator terminator,
            std::shared_ptr<perfetto_sdk_helper_t> perfetto_sdk_helper)
        {
            return std::make_shared<perfetto_agent_t<perfetto_sdk_helper_t>>(io_context,
                                                                             std::move(ipc_sink),
                                                                             std::move(terminator),
                                                                             perfetto_sdk_helper);
        }

        perfetto_agent_t(boost::asio::io_context & io_context,
                         std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink,
                         [[maybe_unused]] agent_environment_base_t::terminator terminator,
                         std::shared_ptr<perfetto_sdk_helper_t> perfetto_sdk_helper)
            : io_context(io_context),
              strand(io_context),
              ipc_sink(std::move(ipc_sink)),
              perfetto_sdk_helper(perfetto_sdk_helper),
              buffer(buffer_sz, '\0')
        {
            graphics_property_value = android_prop_utils::readProperty(GRAPHICS_PROFILER_PROPERTY.data(), false);
            if (!android_prop_utils::setProperty(GRAPHICS_PROFILER_PROPERTY.data(),
                                                 GRAPHICS_PROFILER_PROPERTY_VALUE.data())) {
                LOG_WARNING("Failed to set graphics property %s", GRAPHICS_PROFILER_PROPERTY.data());
            }
            perfetto_sdk_helper->initialize_sdk();
        }

        ~perfetto_agent_t()
        {
            if (graphics_property_value) {
                android_prop_utils::setProperty(GRAPHICS_PROFILER_PROPERTY.data(),
                                                graphics_property_value.value().c_str());
            }
        }

        async::continuations::polymorphic_continuation_t<> co_shutdown()
        {
            LOG_DEBUG("Got a shutdown request");
            using namespace async::continuations;
            return start_on(strand) | then([self = this->shared_from_this()]() mutable -> polymorphic_continuation_t<> {
                       if (std::exchange(self->is_shutdown, true)) {
                           return {};
                       }
                       return self->cont_shutdown();
                   });
        }

        async::continuations::polymorphic_continuation_t<> co_receive_message(ipc::msg_perfetto_close_conn_t /*msg*/)
        {
            using namespace async::continuations;

            return start_on(strand) | then([self = this->shared_from_this()]() mutable -> polymorphic_continuation_t<> {
                       self->perfetto_sdk_helper->stop_sdk();
                       return {};
                   });
        }

        async::continuations::polymorphic_continuation_t<> co_receive_message(const ipc::msg_monitored_pids_t & /*msg*/)
        {
            LOG_DEBUG("Got monitored pids message");

            if (!perfetto_sdk_helper->start_trace()) {
                LOG_ERROR("Could not start the perfetto trace. This agent will shut down.");
                return co_shutdown();
            }

            spawn("Perfetto Read Loop", co_send_initial_frame());
            return {};
        }

    private:
        boost::asio::io_context & io_context;
        boost::asio::io_context::strand strand;
        std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink;
        bool is_shutdown {false};
        std::optional<std::string> graphics_property_value;

        std::shared_ptr<perfetto_sdk_helper_t> perfetto_sdk_helper {};

        std::vector<char> buffer;

        static constexpr std::string_view GRAPHICS_PROFILER_PROPERTY = "debug.graphics.gpu.profiler.perfetto";
        static constexpr std::string_view GRAPHICS_PROFILER_PROPERTY_VALUE = "1";

        async::continuations::polymorphic_continuation_t<> cont_shutdown()
        {
            using namespace async::continuations;
            return start_on(strand) //
                 | then([self = this->shared_from_this()]() mutable {
                       self->is_shutdown = true;
                       self->perfetto_sdk_helper->stop_sdk();
                   });
        }

        async::continuations::polymorphic_continuation_t<> co_send_initial_frame()
        {
            using namespace async::continuations;
            return start_on(strand) //
                 | then([self = this->shared_from_this()]() {
                       std::vector<char> payload(self->protocol_handshake_tag.begin(),
                                                 self->protocol_handshake_tag.end());
                       return start_on(self->strand) //
                            | self->ipc_sink->async_send_message(ipc::msg_perfetto_recv_bytes_t {std::move(payload)},
                                                                 use_continuation)
                            | then([self](const auto & ec, auto /*msg*/) mutable -> polymorphic_continuation_t<> {
                                  if (ec) {
                                      LOG_ERROR("Failed to send perfetto handshake frame: %s", ec.message().c_str());
                                      return self->co_shutdown();
                                  }
                                  return self->co_read_perfetto_trace();
                              });
                   });
        }

        async::continuations::polymorphic_continuation_t<> co_forward_to_shell(std::size_t size)
        {
            using namespace async::continuations;
            if (size == 0) {
                return co_read_perfetto_trace();
            }

            auto range_start = buffer.begin();
            auto range_end = range_start + size;

            std::vector<char> payload(range_start, range_end);
            return ipc_sink->async_send_message(ipc::msg_perfetto_recv_bytes_t {std::move(payload)}, use_continuation)
                 | then([self = this->shared_from_this()](const auto & err,
                                                          auto /*msg*/) mutable -> polymorphic_continuation_t<> {
                       if (err) {
                           LOG_ERROR("Could not send perfetto data to the gatord shell instance: %s",
                                     err.message().c_str());
                           return self->co_shutdown();
                       }

                       if (self->is_shutdown) {
                           LOG_TRACE("Shutdown requested - breaking out of perfetto read loop");
                           return {};
                       }

                       return self->co_read_perfetto_trace();
                   });
        }

        auto co_read_perfetto_trace()
        {
            using namespace async::continuations;
            auto self = this->shared_from_this();

            return start_on(strand)
                 | do_if([self]() { return !self->is_shutdown; },
                         [self]() {
                             return start_on(self->strand)
                                  | self->perfetto_sdk_helper->async_read_trace(
                                      {self->buffer.data(), self->buffer.size()},
                                      use_continuation)
                                  | then([self](auto err, auto size) {
                                        if (err) {
                                            LOG_ERROR("Received an error while trying to read perfetto data: %s",
                                                      err.message().c_str());
                                            return self->co_shutdown();
                                        }
                                        return self->co_forward_to_shell(size);
                                    });
                         });
        }
    };
};
