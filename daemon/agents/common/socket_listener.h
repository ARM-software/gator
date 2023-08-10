/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"

#include <memory>
#include <type_traits>
#include <utility>

#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v6.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/v6_only.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/system/system_error.hpp>

namespace agents {
    /** Base class for socket_listener_t */
    class socket_listener_base_t {
    public:
        virtual ~socket_listener_base_t() noexcept = default;
        /** Was the socket opened correctly */
        [[nodiscard]] virtual bool is_open() const = 0;
        /** Start async accepting of connections */
        virtual void start() = 0;
        /** Close the listener connection */
        virtual void close() = 0;
    };

    /** Set any options on a UDS socket */
    inline void set_acceptor_options(boost::asio::local::stream_protocol::acceptor & /*acceptor*/)
    {
    }

    /** Set any options on a TCP socket */
    inline void set_acceptor_options(boost::asio::ip::tcp::acceptor & acceptor)
    {
        if (acceptor.local_endpoint().protocol() == boost::asio::ip::tcp::v6()) {
            acceptor.set_option(boost::asio::ip::v6_only(true));
        }
        acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    }

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

        [[nodiscard]] bool is_open() const override { return socket_acceptor.is_open(); }

        void start() override { do_accept(); }

        void close() override { socket_acceptor.close(); };

    private:
        worker_spawner_type worker_spawner;
        acceptor_type socket_acceptor;

        socket_listener_t(worker_spawner_type && worker_spawner,
                          boost::asio::io_context & ctx,
                          endpoint_type const & endpoint)
            : worker_spawner(std::forward<worker_spawner_type>(worker_spawner)), socket_acceptor(ctx)
        {

            socket_acceptor.open(endpoint.protocol());
            set_acceptor_options(socket_acceptor);
            socket_acceptor.bind(endpoint);
            socket_acceptor.listen();
        }

        void do_accept()
        {
            LOG_TRACE("(%p) Waiting to accept connection on socket %d", this, socket_acceptor.native_handle());

            socket_acceptor.async_accept([st = this->shared_from_this()](auto ec, auto socket) {
                // handle error
                if (ec) {
                    if (ec != boost::asio::error::operation_aborted) {
                        LOG_ERROR("(%p) Error occured accepting new connection for %d due to %s",
                                  st.get(),
                                  st->socket_acceptor.native_handle(),
                                  ec.message().c_str());
                    }
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

    /** Make a UDS socket listener for some endpoint with the supplied worker fn */
    template<typename WorkerSpawnerFn>
    auto make_uds_socket_lister(WorkerSpawnerFn && worker_spawner,
                                boost::asio::io_context & ctx,
                                typename uds_socket_lister_t<WorkerSpawnerFn>::endpoint_type const & endpoint)
    {
        using listener_t = uds_socket_lister_t<WorkerSpawnerFn>;

        try {
            return listener_t::create(std::forward<WorkerSpawnerFn>(worker_spawner), ctx, endpoint);
        }
        catch (boost::system::system_error const & ex) {
            LOG_WARNING("Failed to create new UDS socket listener due to %s", ex.what());
            return std::shared_ptr<listener_t> {};
        }
    }

    /** Make a TCP socket listener for some endpoint with the supplied worker fn */
    template<typename WorkerSpawnerFn>
    auto make_tcp_socket_lister(WorkerSpawnerFn && worker_spawner,
                                boost::asio::io_context & ctx,
                                typename tcp_socket_lister_t<WorkerSpawnerFn>::endpoint_type const & endpoint)
    {
        using listener_t = tcp_socket_lister_t<WorkerSpawnerFn>;

        try {
            return listener_t::create(std::forward<WorkerSpawnerFn>(worker_spawner), ctx, endpoint);
        }
        catch (boost::system::system_error const & ex) {
            LOG_WARNING("Failed to create new TCP socket listener due to %s", ex.what());
            return std::shared_ptr<listener_t> {};
        }
    }
}
