/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/async_initiate.h"
#include "ipc/messages.h"

#include <functional>

#include <boost/system/error_code.hpp>

#include <unistd.h>

namespace agents {

    /**
     * Base interface for agent process workers
     */
    class i_agent_worker_t {
    public:
        /** Enumerates the possible states the agent can be in */
        enum class state_t {
            launched,
            ready,
            shutdown_requested,
            shutdown_received,
            terminated_pending_message_loop,
            terminated,
        };

        /** Callback used to consume state changes */
        using state_change_observer_t = std::function<void(pid_t, state_t, state_t)>;

        virtual ~i_agent_worker_t() noexcept = default;
        virtual void on_sigchild() = 0;
        virtual void shutdown() = 0;

        /**
         * Asynchronously send an IPC message to the agent.
         *
         * @tparam MessageType IPC message type
         * @tparam CompletionToken Continuation or callback to handle the result
         * @param message Message to send
         * @param io_context Context to post the result handling to
         * @param token Completion token instance
         * @return Continuation or void if the CompletionToken is a callback
         */
        template<typename MessageType, typename CompletionToken>
        auto async_send_message(MessageType message, boost::asio::io_context & io_context, CompletionToken && token)
        {
            using namespace async::continuations;
            using message_type = std::decay_t<MessageType>;
            static_assert(ipc::is_ipc_message_type_v<message_type>, "MessageType must be an IPC type");

            return async_initiate_explicit<void(boost::system::error_code)>(
                [&, message = std::move(message)](auto && sc) {
                    this->async_send_message(
                        ipc::all_message_types_variant_t {std::move(message)},
                        io_context,
                        stored_continuation_t<boost::system::error_code> {std::forward<decltype(sc)>(sc)});
                },
                std::forward<CompletionToken>(token));
        }

    protected:
        /** Implements the message sending.
         *
         * @param message Message to send
         * @param io_context Context to post the result handling to
         * @param sc Stored continuation holding the user's result handler
         */
        virtual void async_send_message(ipc::all_message_types_variant_t message,
                                        boost::asio::io_context & io_context,
                                        async::continuations::stored_continuation_t<boost::system::error_code> sc) = 0;
    };
}
