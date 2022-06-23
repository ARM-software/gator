/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "agents/agent_environment.h"
#include "agents/perf/capture_configuration.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "ipc/messages.h"
#include "ipc/raw_ipc_channel_sink.h"
#include "ipc/raw_ipc_channel_source.h"
#include "lib/exception.h"

#include <memory>

#include <boost/asio/io_context.hpp>

namespace agents::perf {

    template<typename CaptureType>
    class perf_agent_t : public std::enable_shared_from_this<perf_agent_t<CaptureType>> {
    public:
        using accepted_message_types = std::tuple<ipc::msg_capture_configuration_t, ipc::msg_start_t>;

        using capture_factory =
            std::function<std::shared_ptr<CaptureType>(boost::asio::io_context &,
                                                       async::proc::process_monitor_t & process_monitor,
                                                       std::shared_ptr<ipc::raw_ipc_channel_sink_t>,
                                                       agent_environment_base_t::terminator,
                                                       std::shared_ptr<perf_capture_configuration_t>)>;

        static std::shared_ptr<perf_agent_t<CaptureType>> create(boost::asio::io_context & io,
                                                                 async::proc::process_monitor_t & process_monitor,
                                                                 std::shared_ptr<ipc::raw_ipc_channel_sink_t> sink,
                                                                 agent_environment_base_t::terminator terminator,
                                                                 capture_factory factory)
        {
            return std::shared_ptr<perf_agent_t<CaptureType>>(new perf_agent_t<CaptureType>(io,
                                                                                            process_monitor,
                                                                                            std::move(sink),
                                                                                            std::move(terminator),
                                                                                            std::move(factory)));
        }

        async::continuations::polymorphic_continuation_t<> co_shutdown()
        {
            using namespace async::continuations;

            return start_with() //
                 | then([st = this->shared_from_this()]() -> polymorphic_continuation_t<> {
                       if (st->capture) {
                           return st->capture->async_shutdown(use_continuation);
                       }
                       return {};
                   });
        }

        async::continuations::polymorphic_continuation_t<> co_receive_message(ipc::msg_start_t msg)
        {
            return capture->async_on_received_start_message(msg.header, async::continuations::use_continuation);
        }

        async::continuations::polymorphic_continuation_t<> co_receive_message(ipc::msg_capture_configuration_t msg)
        {
            using namespace async::continuations;

            return start_with() //
                 | then([st = this->shared_from_this(), msg = std::move(msg)]() mutable {
                       LOG_DEBUG("Got capture config message");
                       st->capture = st->wrapped_factory(std::move(msg));

                       // wrapped_factory is one-shot as its contents are moved, so make that explicit by defaulting it
                       st->wrapped_factory = {};

                       return st->capture->async_prepare(async::continuations::use_continuation);
                   });
        }

    private:
        std::function<std::shared_ptr<CaptureType>(ipc::msg_capture_configuration_t)> wrapped_factory;
        std::shared_ptr<CaptureType> capture;

        perf_agent_t(boost::asio::io_context & io,
                     async::proc::process_monitor_t & process_monitor,
                     std::shared_ptr<ipc::raw_ipc_channel_sink_t> sink,
                     agent_environment_base_t::terminator terminator,
                     capture_factory factory)
            : wrapped_factory {[sink = std::move(sink), //
                                terminator = std::move(terminator),
                                factory = std::move(factory),
                                &io,
                                &process_monitor](auto msg) mutable {
                  return factory(io,
                                 process_monitor,
                                 std::move(sink),
                                 std::move(terminator),
                                 parse_capture_configuration_msg(std::move(msg)));
              }}
        {
        }
    };
}
