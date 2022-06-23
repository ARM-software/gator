/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include <functional>
#include <memory>

#include <Logging.h>
#include <async/async_buffer.hpp>
#include <ipc/messages.h>
#include <ipc/raw_ipc_channel_sink.h>

namespace apc {

    /*
     * Consumer for the intermediate buffer.
     *
     * Consumes the buffer and creates  APC IPC messages.
     * The ipc message will be send to ipc sink.
     *
     * Consumer will register to the asyn_buffer after each ipc message is send successfully.
     *
     */
    class intermediate_buffer_consumer_t : public std::enable_shared_from_this<intermediate_buffer_consumer_t> {

    public:
        intermediate_buffer_consumer_t(std::shared_ptr<async::async_buffer_t> async_buffer,
                                       std::shared_ptr<ipc::raw_ipc_channel_sink_t> sender)
            : async_buffer(async_buffer), sender(sender) {};

        /**
         * Start consuming, has to be called only once.
         */
        template<typename CompletionToken>
        auto async_start_consuming(CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [st = shared_from_this()](auto && handler) {
                    st->do_async_start_consuming(std::forward<decltype(handler)>(handler));
                },
                token);
        }

        /**
         * Call to terminate the apc consumer .
         * This will make the consumer to stops sending ipc message, and buffer
         * does not get consumed.
         */
        void terminate() { terminated.store(true, std::memory_order_release); }

    private:
        std::atomic<bool> terminated {false};
        std::shared_ptr<async::async_buffer_t> async_buffer;
        std::shared_ptr<ipc::raw_ipc_channel_sink_t> sender;

        /**
         * Registers for async_consume
         * And upon notification calls on_has_buffer_data()
         */
        template<typename Handler>
        void do_async_start_consuming(Handler && handler)
        {
            if (terminated.load(std::memory_order_acquire)) {
                LOG_DEBUG("Intermitted buffer consumer is terminated.");
                handler(boost::system::error_code {});
            }
            else {
                async_buffer->async_consume(
                    [st = weak_from_this(),
                     handler = std::forward<Handler>(handler)](auto success, auto && buffer, auto && action) mutable {
                        auto this_obj = st.lock();
                        if (this_obj) {
                            this_obj->on_has_buffer_data(success,
                                                         std::forward<decltype(buffer)>(buffer),
                                                         std::forward<decltype(action)>(action),
                                                         std::move(handler));
                        }
                        else {
                            LOG_DEBUG("Could not consume buffer data, as this pointer is removed");
                        }
                    });
            }
        }

        /**
         * Consumes the buffer if is ready (success ie true) to create apc ipc message,
         * Ipc message is send to sink, if it successful (error code = {}) , marks the buffer as consumed
         * and re registers for next set of bytes from the buffer.
         *
         * If the consumer is terminated the handler is called with error_code {}.
         *
         */
        template<typename Buffer, typename Action, typename Handler>
        void on_has_buffer_data(bool success, Buffer && buffer, Action && action, Handler && handler)
        {
            if (terminated.load(std::memory_order_acquire)) {
                LOG_DEBUG("Intermitted buffer consumer is terminated.");
                handler(boost::system::error_code {});
            }
            else if (success) {
                //intermediate buffer calls commit for its commit action (endFrame) after each frame.
                sender->async_send_message(ipc::msg_apc_frame_data_from_span_t {std::move(buffer)},
                                           [st = shared_from_this(),
                                            handler = std::forward<Handler>(handler),
                                            action = std::forward<Action>(action)](auto ec, auto /*msg*/) mutable {
                                               if (!ec) {
                                                   // success, mark action complete,
                                                   action.consume();
                                                   //re-regsiter for new bytes from async_buffer
                                                   st->do_async_start_consuming(std::move(handler));
                                               }
                                               else {
                                                   // error, terminate
                                                   LOG_DEBUG("Failed to send apc ipc message due to %s",
                                                             ec.message().c_str());
                                                   handler(ec);
                                               }
                                           });
            }
            else {
                // log and call the handler as now terminating
                LOG_DEBUG("Failed to read from the intermitted buffer.");
                handler(boost::asio::error::make_error_code(boost::asio::error::basic_errors::no_buffer_space));
            }
        }
    };
}
