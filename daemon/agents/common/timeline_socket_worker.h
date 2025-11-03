/* Copyright (C) 2025 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "agents/common/socket_reference.h"
#include "agents/gpu_timeline/endpoint_registry_message.hpp"
#include "agents/gpu_timeline/message_header.hpp"
#include "async/continuations/async_initiate.h"
#include "async/continuations/stored_continuation.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/write.hpp>
#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

namespace agents {

    using boost::system::error_code;

    /**
     * Socket worker responsible for:
     * - reading GPU Timeline data from a socket
     * - forwarding this data in the form of IPC messages into the IPC sink.
     *
     * Timeline data usually comes from a layer driver loaded into the
     * monitored application.
     *
     * @tparam IpcSinkType IPC sink wrapper. Handles sending async IPC messages
     * containing the received data.
     */
    template<typename IpcSinkType>
    class timeline_socket_worker_t : public std::enable_shared_from_this<timeline_socket_worker_t<IpcSinkType>> {
    public:
        using ipc_sink_type = IpcSinkType;

        /** Factory method */
        static std::shared_ptr<timeline_socket_worker_t> create(boost::asio::io_context & context,
                                                                ipc_sink_type && ipc_sink,
                                                                std::shared_ptr<socket_reference_base_t> socket_ref)
        {
            return std::make_shared<timeline_socket_worker_t>(
                timeline_socket_worker_t {context, std::move(ipc_sink), std::move(socket_ref)});
        }

        /** @return True if the socket is still open */
        [[nodiscard]] bool is_open() const { return socket_ref->is_open(); }

        /** Start receiving data from the socket */
        void start()
        {
            // tell of the new connection
            ipc_sink.async_send_new_connection([st = this->shared_from_this()](error_code const & ec, auto /*msg*/) {
                if (ec) {
                    // log it and close the connection
                    LOG_ERROR_IF_NOT_EOF_OR_CANCELLED(
                        ec,
                        "(%p) Error occurred while notifying IPC of new external connection %d, dropping due to %s",
                        st.get(),
                        st->socket_ref->native_handle(),
                        ec.message().c_str());
                    st->async_close([] {});
                    return;
                }

                st->ipc_sink.async_send_estate_header([st](error_code const & ec, auto /*msg*/) {
                    if (ec) {
                        // log it and close the connection
                        LOG_ERROR_IF_NOT_EOF_OR_CANCELLED(
                            ec,
                            "(%p) Error occurred while sending ESTATE header to shell for external "
                            "connection %d, dropping due to %s",
                            st.get(),
                            st->socket_ref->native_handle(),
                            ec.message().c_str());
                        st->async_close([] {});
                        return;
                    }

                    // wait for incoming data
                    st->read_message();
                });
            });
        }

        /** Close the connection (adapted from socket_worker.h) */
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

        timeline_socket_worker_t(boost::asio::io_context & context,
                                 ipc_sink_type && ipc_sink,
                                 std::shared_ptr<socket_reference_base_t> socket_ref)
            : context(context), ipc_sink(std::move(ipc_sink)), socket_ref(std::move(socket_ref))
        {
        }

        /** Perform the async close operation (adapted from socket_worker.h) */
        template<typename R, typename E>
        void do_async_close(async::continuations::raw_stored_continuation_t<R, E> && sc)
        {
            // tell the IPC mechanism, but only once
            if (is_open()) {
                return ipc_sink.async_send_close_connection(
                    [st = this->shared_from_this(), sc = std::move(sc)](error_code const & /*ec*/,
                                                                        auto /*msg*/) mutable {
                        // close the socket
                        st->socket_ref->close();

                        LOG_DEBUG("(%p) Was closed", st.get());

                        // notify the handler
                        resume_continuation(st->context, std::move(sc));
                        return;
                    });
            }

            // otherwise just call the handler directly
            resume_continuation(context, std::move(sc));
        }

        /** Reads a message from the socket */
        void read_message()
        {
            auto message_header_serialized =
                std::make_shared<std::array<char, gpu_timeline::message_header_t::SERIALIZED_LENGTH>>();

            socket_ref->with_socket([st = this->shared_from_this(), message_header_serialized](auto & socket) {
                async_read(
                    socket,
                    boost::asio::buffer(*message_header_serialized),
                    [st, &socket, message_header_serialized](error_code const & ec, size_t /*n_read*/) mutable {
                        if (ec) {
                            LOG_ERROR_IF_NOT_EOF_OR_CANCELLED(ec,
                                                              "(%p) Error occurred reading bytes from "
                                                              "timeline socket %d, dropping due to %s",
                                                              st.get(),
                                                              st->socket_ref->native_handle(),
                                                              ec.message().c_str());
                            st->async_close([] {});
                            return;
                        }

                        auto message_header =
                            std::make_shared<gpu_timeline::message_header_t>(*message_header_serialized);
                        std::optional<uint64_t> message_pair_id = message_header->get_message_pair_id();

                        // If this message requires a response...
                        if (message_pair_id.has_value()) {
                            if (message_header->get_endpoint()
                                != gpu_timeline::message_header_t::endpoint_t::LIST_ENDPOINTS) {
                                LOG_ERROR_IF_NOT_EOF_OR_CANCELLED(
                                    ec,
                                    "(%p) Response requested by remote party, but not requesting endpoints "
                                    "(actual endpoint ID = %d)",
                                    st.get(),
                                    static_cast<int>(message_header->get_endpoint()));
                                st->async_close([] {});
                                return;
                            }

                            st->write_endpoint_registry_to_socket(*message_pair_id, socket);
                        }
                        else if (message_header->get_endpoint()
                                 == gpu_timeline::message_header_t::endpoint_t::TIMELINE) {
                            // Read message payload

                            // uint8_t and char are convenient for different APIs. Some APIs use
                            // char for compatibility with other APIs printing strings.
                            auto message_body_serialized =
                                std::make_shared<std::vector<uint8_t>>(message_header->get_payload_size());

                            async_read(socket,
                                       boost::asio::buffer(*message_body_serialized),
                                       [st, message_body_serialized](error_code const & ec, size_t /*n_read*/) mutable {
                                           if (ec) {
                                               LOG_ERROR_IF_NOT_EOF_OR_CANCELLED(
                                                   ec,
                                                   "(%p) Error occurred reading timeline data from "
                                                   "timeline socket %d, dropping due to %s",
                                                   st.get(),
                                                   st->socket_ref->native_handle(),
                                                   ec.message().c_str());
                                               st->async_close([] {});
                                               return;
                                           }

                                           st->send_timeline_data_to_shell(message_body_serialized);
                                       });
                        }
                        else {
                            LOG_ERROR_IF_NOT_EOF_OR_CANCELLED(
                                ec,
                                "(%p) Timeline protocol endpoint ID unrecognized/unexpected (%d)",
                                st.get(),
                                static_cast<int>(message_header->get_endpoint()));
                            st->async_close([] {});
                        }
                    });
            });
        }

        template<typename Socket>
        void write_endpoint_registry_to_socket(uint64_t message_pair_id, Socket & socket)
        {
            auto st = this->shared_from_this();

            LOG_TRACE("(%p) Sending endpoint registry to socket", st.get());

            using gpu_timeline::endpoint_registry_message_t;

            auto return_message = std::make_shared<endpoint_registry_message_t>(message_pair_id);
            auto return_message_serialized =
                std::make_shared<std::vector<lib::Span<const char>>>(return_message->get_serialized());

            // NOTE: The nested async_write() calls below are inelegant. The intention was to have
            // one call to async_write(), taking the whole of return_message->get_serialized().
            // That would write several buffers at once using ASIO's scatter-gather I/O feature -
            // see https://www.boost.org/doc/libs/1_88_0/doc/html/boost_asio/reference/buffer.html
            // For an unknown reason, that compiles but writes the wrong number of bytes. As such,
            // we use additional calls, variables and types to write each buffer independently.
            async_write(socket,
                        boost::asio::buffer(return_message_serialized->at(
                            endpoint_registry_message_t::serialized_components_t::HEADER)),
                        [st, &socket, return_message, return_message_serialized](error_code const & ec,
                                                                                 size_t /*n_written*/) mutable {
                            if (ec) {
                                LOG_ERROR("(%p) Failed to write message header to socket: %s",
                                          st.get(),
                                          ec.what().c_str());
                                st->async_close([] {});
                                return;
                            }

                            async_write(socket,
                                        boost::asio::buffer(return_message_serialized->at(
                                            endpoint_registry_message_t::serialized_components_t::BODY)),
                                        [st](error_code const & ec, size_t /*n_written*/) mutable {
                                            if (ec) {
                                                LOG_ERROR("(%p) Failed to write endpoint registry to socket: %s",
                                                          st.get(),
                                                          ec.what().c_str());
                                                st->async_close([] {});
                                                return;
                                            }

                                            // We're done with this message: read another
                                            st->read_message();
                                        });
                        });
        }

        /**
         * Forward some received timeline data to the shell process via IPC
         * @param message_body_serialized Data to be written
         */
        void send_timeline_data_to_shell(std::shared_ptr<std::vector<uint8_t>> message_body_serialized)
        {
            return ipc_sink.async_send_received_bytes(
                message_body_serialized,
                [st = this->shared_from_this()](error_code const & ec, auto /*msg*/) mutable {
                    if (ec) {
                        LOG_ERROR_IF_NOT_EOF_OR_CANCELLED(
                            ec,
                            "(%p) Error occurred forwarding bytes for external connection %d, dropping due to %s",
                            st.get(),
                            st->socket_ref->native_handle(),
                            ec.message().c_str());
                        st->async_close([] {});
                        return;
                    }

                    // We're done with this message: read another
                    st->read_message();
                });
        }
    };

} // namespace agents
