/* Copyright (C) 2021-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "agents/common/socket_reference.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/stored_continuation.h"
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
        static std::shared_ptr<socket_read_worker_t> create(boost::asio::io_context & context,
                                                            ipc_sink_type && ipc_sink,
                                                            std::shared_ptr<socket_reference_base_t> socket_ref)
        {
            return std::make_shared<socket_read_worker_t>(
                socket_read_worker_t {context, std::move(ipc_sink), std::move(socket_ref)});
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
                    LOG_ERROR_IF_NOT_EOF_OR_CANCELLED(
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
        auto async_send_bytes(std::vector<std::uint8_t> && bytes, CompletionToken && token)
        {
            using namespace async::continuations;

            LOG_TRACE("(%p) Received request to send %zu bytes", this, bytes.size());

            return async_initiate_explicit<void(boost::system::error_code)>(
                [st = this->shared_from_this(), bytes = std::move(bytes)](auto && sc) mutable {
                    return st->do_async_send_bytes(std::move(bytes), std::forward<decltype(sc)>(sc));
                },
                std::forward<CompletionToken>(token));
        }

        /** Close the connection */
        template<typename CompletionToken>
        auto async_close(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void()>(
                [st = this->shared_from_this()](auto && sc) mutable {
                    st->do_async_close(std::forward<decltype(sc)>(sc));
                },
                std::forward<CompletionToken>(token));
        }

    private:
        boost::asio::io_context & context;
        ipc_sink_type ipc_sink;
        std::shared_ptr<socket_reference_base_t> socket_ref;
        std::vector<std::uint8_t> receive_message_buffer;

        socket_read_worker_t(boost::asio::io_context & context,
                             ipc_sink_type && ipc_sink,
                             std::shared_ptr<socket_reference_base_t> socket_ref)
            : context(context), ipc_sink(std::move(ipc_sink)), socket_ref(std::move(socket_ref))
        {
        }

        /** Perform the async close operation */
        template<typename R, typename E>
        void do_async_close(async::continuations::raw_stored_continuation_t<R, E> && sc)
        {
            // tell the IPC mechanism, but only once
            if (is_open()) {
                return ipc_sink.async_send_close_connection(
                    [st = this->shared_from_this(), sc = std::move(sc)](auto const & /*ec*/, auto /*msg*/) mutable {
                        // close the socket
                        st->socket_ref->close();
                        // notify the handler
                        return resume_continuation(st->context, std::move(sc));
                    });
            }

            // otherwise just call the handler directly
            return resume_continuation(context, std::move(sc));
        }

        /** Perform the async send operation */
        template<typename R, typename E>
        void do_async_send_bytes(std::vector<std::uint8_t> && bytes,
                                 async::continuations::raw_stored_continuation_t<R, E, boost::system::error_code> && sc)
        {
            using namespace async::continuations;

            socket_ref->with_socket([st = this->shared_from_this(),
                                     bytes_ptr = std::make_unique<std::vector<std::uint8_t>>(std::move(bytes)),
                                     sc = std::move(sc)](auto & socket) mutable {
                // make the buffer before the call to move(bytes_ptr) otherwise the move will happen before the deref
                auto buffer = boost::asio::buffer(*bytes_ptr);

                LOG_TRACE("(%p) Sending %zu bytes", st.get(), bytes_ptr->size());

                boost::asio::async_write(
                    socket,
                    buffer,
                    // write result handler
                    [st, bytes_ptr = std::move(bytes_ptr), sc = std::move(sc), &socket] //
                    (auto const & ec, auto n_written) mutable {
                        // handle send error?
                        if (ec) {
                            // log it and close the connection
                            LOG_ERROR_IF_NOT_EOF_OR_CANCELLED(ec,
                                                              "(%p) Error occured forwarding bytes to external "
                                                              "connection %d, dropping due to %s",
                                                              st.get(),
                                                              socket.native_handle(),
                                                              ec.message().c_str());

                            return submit(st->context,
                                          st->async_close(use_continuation) | then([ec]() { return ec; }),
                                          std::move(sc));
                        }

                        // send length error
                        if (n_written != bytes_ptr->size()) {
                            // log it and close the connection
                            LOG_ERROR("(%p) Error occured forwarding bytes to external "
                                      "connection %d, dropping due to "
                                      "short write",
                                      st.get(),
                                      socket.native_handle());

                            // pass EOF error code to handler
                            return submit(st->context,
                                          st->async_close(use_continuation) | then([]() {
                                              return boost::asio::error::make_error_code(
                                                  boost::asio::error::misc_errors::eof);
                                          }),
                                          std::move(sc));
                        }

                        LOG_TRACE("(%p) Sent %zu bytes", st.get(), n_written);

                        // wait for the next message
                        return resume_continuation(st->context, std::move(sc), boost::system::error_code {});
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
                            LOG_ERROR_IF_NOT_EOF_OR_CANCELLED(
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
                        LOG_ERROR_IF_NOT_EOF_OR_CANCELLED(
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
