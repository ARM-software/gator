/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "agents/common/socket_reference.h"
#include "lib/Assert.h"

#include <array>
#include <memory>
#include <type_traits>
#include <utility>

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>

namespace agents {

    /**
     * Socket worker responsible for reading data from the socket and passing the received data as IPC messages into the ipc sink.
     *
     * @tparam IpcSinkType The IPC sink wrapper which handles sending async IPC messages containing the received data
     */
    template<typename IpcSinkType>
    class socket_read_worker_t : public std::enable_shared_from_this<socket_read_worker_t<IpcSinkType>> {
    public:
        static constexpr std::size_t max_buffer_size {4096};

        using ipc_sink_type = IpcSinkType;

        /** Factory method */
        static std::shared_ptr<socket_read_worker_t> create(ipc_sink_type && ipc_sink,
                                                            std::shared_ptr<socket_reference_base_t> socket_ref)
        {
            return std::make_shared<socket_read_worker_t>(
                socket_read_worker_t {std::move(ipc_sink), std::move(socket_ref)});
        }

        /** @return True if the socket is still open */
        [[nodiscard]] bool is_open() const { return socket_ref->is_open(); }

        /** Start receiving data from the socket */
        void start()
        {
            // tell of the new connection
            ipc_sink.async_send_new_connection([st = this->shared_from_this()](auto const & ec, auto /*msg*/) {
                if (ec) {
                    // log it and close the connection
                    LOG_ERROR_IF_NOT_EOF(
                        ec,
                        "(%p) Error occured while notifying IPC of new external connection %d, dropping due to %s",
                        st.get(),
                        st->socket_ref->native_handle(),
                        ec.message().c_str());
                    return st->async_close([st]() { LOG_DEBUG("(%p) Was closed", st.get()); });
                }
                // wait for incoming data
                st->do_read_bytes();
            });
        }

        /** Send some data to the socket */
        template<typename CompletionToken>
        auto async_send_bytes(std::vector<char> && bytes, CompletionToken && token)
        {
            LOG_TRACE("(%p) Received request to send %zu bytes", this, bytes.size());

            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [st = this->shared_from_this(), bytes = std::move(bytes)](auto && handler) mutable {
                    using Handler = decltype(handler);
                    return st->do_async_send_bytes(std::move(bytes), std::forward<Handler>(handler));
                },
                token);
        }

        /** Close the connection */
        template<typename CompletionToken>
        auto async_close(CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void()>(
                [st = this->shared_from_this()](auto && handler) mutable {
                    using Handler = decltype(handler);
                    st->do_async_close(std::forward<Handler>(handler));
                },
                token);
        }

    private:
        ipc_sink_type ipc_sink;
        std::shared_ptr<socket_reference_base_t> socket_ref;
        std::vector<char> receive_message_buffer {};

        socket_read_worker_t(ipc_sink_type && ipc_sink, std::shared_ptr<socket_reference_base_t> socket_ref)
            : ipc_sink(std::move(ipc_sink)), socket_ref(std::move(socket_ref))
        {
        }

        /** Perform the async close operation */
        template<typename Handler>
        void do_async_close(Handler && handler)
        {
            using handler_type = std::decay_t<Handler>;

            // tell the IPC mechanism, but only once
            if (is_open()) {
                return ipc_sink.async_send_close_connection(
                    [st = this->shared_from_this(), handler = std::forward<handler_type>(handler)](auto const & /*ec*/,
                                                                                                   auto /*msg*/) {
                        // close the socket
                        st->socket_ref->close();
                        // notify the handler
                        return handler();
                    });
            }

            // otherwise just call the handler directly
            return handler();
        }

        /** Perform the async send operation */
        template<typename Handler>
        void do_async_send_bytes(std::vector<char> && bytes, Handler && handler)
        {
            using handler_type = std::decay_t<Handler>;

            socket_ref->with_socket([st = this->shared_from_this(),
                                     bytes_ptr = std::make_unique<std::vector<char>>(std::move(bytes)),
                                     handler = std::forward<handler_type>(handler)](auto & socket) mutable {
                // make the buffer before the call to move(bytes_ptr) otherwise the move will happen before the deref
                auto buffer = boost::asio::buffer(*bytes_ptr);

                LOG_TRACE("(%p) Sending %zu bytes", st.get(), bytes_ptr->size());

                boost::asio::async_write(
                    socket,
                    buffer,
                    // write result handler
                    [st, bytes_ptr = std::move(bytes_ptr), handler = std::forward<handler_type>(handler), &socket] //
                    (auto const & ec, auto n_written) mutable {
                        // handle send error?
                        if (ec) {
                            // log it and close the connection
                            LOG_ERROR_IF_NOT_EOF(ec,
                                                 "(%p) Error occured forwarding bytes to external "
                                                 "connection %d, dropping due to %s",
                                                 st.get(),
                                                 socket.native_handle(),
                                                 ec.message().c_str());
                            return st->async_close([ec, handler = std::forward<handler_type>(handler)]() {
                                // pass the error code to the handler
                                handler(ec);
                            });
                        }

                        // send length error
                        if (n_written != bytes_ptr->size()) {
                            // log it and close the connection
                            LOG_ERROR("(%p) Error occured forwarding bytes to external "
                                      "connection %d, dropping due to "
                                      "short write",
                                      st.get(),
                                      socket.native_handle());
                            return st->async_close([handler = std::forward<handler_type>(handler)]() {
                                // pass EOF error code to handler
                                handler(boost::asio::error::make_error_code(boost::asio::error::misc_errors::eof));
                            });
                        }

                        LOG_TRACE("(%p) Sent %zu bytes", st.get(), n_written);

                        // wait for the next message
                        return handler(boost::system::error_code {});
                    });
            });
        }

        /** Wait to receive some bytes from the external connection */
        void do_read_bytes()
        {
            // the last read may have resized or otherwise modified the buffer;
            // reset it to a known state (contents don't matter, just size)
            // before the next read.
            receive_message_buffer.resize(max_buffer_size);

            // read some data
            socket_ref->with_socket([st = this->shared_from_this()](auto & socket) {
                socket.async_read_some(
                    boost::asio::buffer(st->receive_message_buffer),
                    [st](auto ec, auto n_read) mutable {
                        if (ec) {
                            // log it and close the connection
                            LOG_ERROR_IF_NOT_EOF(
                                ec,
                                "(%p) Error occured reading bytes for external connection %d, dropping due to %s",
                                st.get(),
                                st->socket_ref->native_handle(),
                                ec.message().c_str());
                            return st->async_close([st]() { LOG_DEBUG("(%p) Was closed", st.get()); });
                        }

                        st->do_forward_inward_bytes(n_read);
                    });
            });
        }

        /** Forward some received bytes to the shell process via IPC */
        void do_forward_inward_bytes(std::size_t n_to_write)
        {
            if (n_to_write <= 0) {
                return do_read_bytes();
            }

            // set the size of the vector to match the number of bytes to send
            // the underlying capacity /shouldn't/ change as n_to_write should
            // be <= capacity, so no need to worry about realloc...
            receive_message_buffer.resize(n_to_write);

            return ipc_sink.async_send_received_bytes(
                std::move(receive_message_buffer),
                [st = this->shared_from_this()](auto const & ec, auto msg) mutable {
                    // reuse the buffer
                    st->receive_message_buffer = ipc_sink_type::reclaim_buffer(std::move(msg));
                    // handle send error?
                    if (ec) {
                        // log it and close the connection
                        LOG_ERROR_IF_NOT_EOF(
                            ec,
                            "(%p) Error occured forwarding bytes for external connection %d, dropping due to %s",
                            st.get(),
                            st->socket_ref->native_handle(),
                            ec.message().c_str());
                        return st->async_close([st]() { LOG_DEBUG("(%p) Was closed", st.get()); });
                    }
                    // get some more bytes
                    st->do_read_bytes();
                });
        }
    };
}
