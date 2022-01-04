/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"

#include <memory>
#include <type_traits>
#include <utility>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/local/stream_protocol.hpp>

namespace pipe_agents {
    /** Base class for socket_listener_t */
    class socket_listener_base_t {
    public:
        virtual ~socket_listener_base_t() noexcept = default;
        /** Start async accepting of connections */
        virtual void start() = 0;
        /** Close the listener connection */
        virtual void close() = 0;
    };

    /**
     * A class that listens for incoming connections on some socket and then spawns some worker for each connection.
     *
     * @tparam ProtocolType The protocol type (e.g. TCP, Unix domain)
     * @tparam WorkerSpawnerFn The function that will spawn a connection worker. Must be non blocking, but may complete synchronously.
     */
    template<typename ProtocolType, typename WorkerSpawnerFn>
    class socket_listener_t : public std::enable_shared_from_this<socket_listener_t<ProtocolType, WorkerSpawnerFn>>,
                              public socket_listener_base_t {
    public:
        using protocol_type = ProtocolType;
        using acceptor_type = typename protocol_type::acceptor;
        using endpoint_type = typename protocol_type::endpoint;
        using socket_type = typename protocol_type::socket;
        using worker_spawner_type = WorkerSpawnerFn;

        static_assert(std::is_invocable_v<worker_spawner_type, socket_type>);

        static std::shared_ptr<socket_listener_t> create(worker_spawner_type && worker_spawner,
                                                         boost::asio::io_context & ctx,
                                                         endpoint_type const & endpoint)
        {
            return std::make_shared<socket_listener_t>(
                socket_listener_t {std::forward<worker_spawner_type>(worker_spawner), ctx, endpoint});
        }

        [[nodiscard]] endpoint_type endpoint() { return socket_acceptor.local_endpoint(); }

        void start() override { do_accept(); }

        void close() override { socket_acceptor.close(); };

    private:
        worker_spawner_type worker_spawner;
        acceptor_type socket_acceptor;

        socket_listener_t(worker_spawner_type && worker_spawner,
                          boost::asio::io_context & ctx,
                          endpoint_type const & endpoint)
            : worker_spawner(std::forward<worker_spawner_type>(worker_spawner)), socket_acceptor(ctx, endpoint)
        {
            socket_acceptor.listen();
        }

        void do_accept()
        {
            LOG_TRACE("(%p) Waiting to accept connection on socket %d", this, socket_acceptor.native_handle());

            socket_acceptor.async_accept([st = this->shared_from_this()](auto ec, auto socket) {
                // handle error
                if (ec) {
                    LOG_ERROR("(%p) Error occured accepting new connection for %d due to %s",
                              st.get(),
                              st->socket_acceptor.native_handle(),
                              ec.message().c_str());
                    return;
                }

                LOG_TRACE("(%p) Accepted new connection on socket %d with id %d",
                          st.get(),
                          st->socket_acceptor.native_handle(),
                          socket.native_handle());

                st->worker_spawner(std::move(socket));

                st->do_accept();
            });
        }
    };

    /** a socket listener that listens on unix domain sockets */
    template<typename WorkerSpawnerFn>
    using uds_socket_lister_t = socket_listener_t<boost::asio::local::stream_protocol, WorkerSpawnerFn>;

    /** a socket listener that listens on tcp sockets */
    template<typename WorkerSpawnerFn>
    using tcp_socket_lister_t = socket_listener_t<boost::asio::ip::tcp, WorkerSpawnerFn>;
}
