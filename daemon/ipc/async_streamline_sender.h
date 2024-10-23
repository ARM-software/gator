/* Copyright (C) 2022-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "ipc/codec.h"
#include "ipc/raw_ipc_channel_sink.h"
#include "ipc/responses.h"
#include "lib/Assert.h"
#include "lib/AutoClosingFd.h"

#include <array>
#include <charconv>
#include <optional>
#include <utility>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/io_context_strand.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>

namespace ipc {

    class async_streamline_sender_t : public std::enable_shared_from_this<async_streamline_sender_t> {

    public:
        /** Factory method */
        static std::shared_ptr<async_streamline_sender_t> create(boost::asio::io_context & io_context,
                                                                 lib::AutoClosingFd && out,
                                                                 bool is_local_capture)
        {
            return std::make_shared<async_streamline_sender_t>(
                async_streamline_sender_t {io_context, std::move(out), is_local_capture});
        }

        /**
         * Write some fixed-size message into the send buffer.
         */
        template<typename Response, typename CompletionToken>
        auto async_send_message(Response message, CompletionToken && token)
        {
            using response_type = std::decay_t<Response>;
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [st = shared_from_this(), message = std::forward<response_type>(message)](auto && handler) mutable {
                    using Handler = decltype(handler);
                    st->do_async_send_message(std::forward<response_type>(message), std::forward<Handler>(handler));
                },
                token);
        }

    private:
        /** The IPC sink, for sending message to the agent */
        std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink;
        /** The stream if local capture */
        std::shared_ptr<boost::asio::posix::stream_descriptor> write_out;
        boost::asio::io_context::strand strand;
        bool is_local_capture = false;

        async_streamline_sender_t(boost::asio::io_context & io_context,
                                  lib::AutoClosingFd && out,
                                  bool is_local_capture)
            : strand(io_context), is_local_capture(is_local_capture)
        {

            if (!is_local_capture) {
                ipc_sink = ipc::raw_ipc_channel_sink_t::create(io_context, std::move(out));
            }
            else {
                write_out = std::make_shared<boost::asio::posix::stream_descriptor>(io_context, out.release());
            }
        }

        template<typename Response, typename Handler>
        void do_async_send_message(Response message, Handler && handler)
        {
            using handler_type = std::decay_t<Handler>;
            static_assert(std::is_same_v<Handler, handler_type>);

            if (is_local_capture) {
                //?? will the 000 file be incremented *000, *001 etc??
                // Write data to disk as long as it is not meta data
                if (message.key == response_type::apc_data) {
                    auto buffer_ptr = std::make_shared<std::vector<uint8_t>>(std::move(message.payload));
                    //payload length bytes
                    auto length = buffer_ptr->size();
                    auto buffer_length = std::make_shared<std::array<uint8_t, 4>>(std::array<uint8_t, 4>({{
                        (static_cast<uint8_t>(length)),
                        (static_cast<uint8_t>(length >> 8)),
                        (static_cast<uint8_t>(length >> 16)),
                        (static_cast<uint8_t>(length >> 24)),
                    }}));
                    //write payload length
                    boost::asio::async_write(
                        *write_out,
                        boost::asio::buffer(*buffer_length),
                        [st = shared_from_this(),
                         buffer_ptr,
                         buffer_length,
                         h = std::forward<decltype(handler)>(handler)](auto const & ec,
                                                                       std::size_t bytes_transferred) mutable { //
                            (void) buffer_length;
                            //write payload
                            boost::asio::async_write(
                                *(st->write_out),
                                boost::asio::buffer(*buffer_ptr),
                                [h = std::forward<decltype(h)>(h)](auto const & ec,
                                                                   std::size_t bytes_transferred) mutable { //
                                    return h(ec);                                                           //
                                });
                            return h(ec);
                        });
                }
            }
            else {
                ipc_sink->async_send_response(
                    std::move(message),
                    [h = std::forward<decltype(handler)>(handler)](auto const & ec, auto const & /* msg */) mutable { //
                        h(ec);                                                                                        //
                    });
            }
        }
    };
}
