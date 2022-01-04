/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#pragma once

#include <memory>
#include <utility>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/local/stream_protocol.hpp>

namespace pipe_agents {
    /** visitor type for socket_reference_base_t */
    class socket_reference_visitor_t {
    public:
        virtual ~socket_reference_visitor_t() noexcept = default;

        virtual void visit(boost::asio::ip::tcp::socket & socket) const = 0;
        virtual void visit(boost::asio::local::stream_protocol::socket & socket) const = 0;
    };

    /** Adapts some handler (e.g. a templated lambda) as a socket_reference_visitor_t */
    template<typename Handler>
    inline auto bind_socket_visitor(Handler handler)
    {
        class binding_t : public socket_reference_visitor_t {
        public:
            explicit binding_t(Handler handler) : handler(std::move(handler)) {}
            void visit(boost::asio::ip::tcp::socket & socket) const override { handler(socket); }
            void visit(boost::asio::local::stream_protocol::socket & socket) const override { handler(socket); }

        private:
            mutable Handler handler;
        };

        return binding_t(std::move(handler));
    }

    /**
     * Socket reference base type used to abstract away the socket type so that it can be stored in a type-erased fashion.
     */
    class socket_reference_base_t {
    public:
        virtual ~socket_reference_base_t() noexcept = default;

        /** @return The native socket handle */
        [[nodiscard]] virtual int native_handle() = 0;

        /** Test if socket is open */
        [[nodiscard]] virtual bool is_open() const = 0;

        /** Close the socket */
        virtual void close() = 0;

        /** Accept the socket visitor */
        virtual void accept(socket_reference_visitor_t const & visitor) = 0;

        /** Convenience function that allows a template lambda to receive the socket reference */
        template<typename Handler>
        void with_socket(Handler handler)
        {
            accept(bind_socket_visitor(std::move(handler)));
        }
    };

    /** A concrete socket reference for some socket type */
    template<typename SocketType>
    class socket_reference_t : public socket_reference_base_t {
    public:
        using socket_type = SocketType;

        explicit socket_reference_t(socket_type socket) : socket(std::move(socket)) {}

        socket_type & operator*() { return socket; }
        socket_type const & operator*() const { return socket; }
        socket_type * operator->() { return &socket; }
        socket_type const * operator->() const { return &socket; }

        [[nodiscard]] int native_handle() override { return socket.native_handle(); }

        [[nodiscard]] bool is_open() const override { return socket.is_open(); }

        void close() override { return socket.close(); }

        void accept(socket_reference_visitor_t const & visitor) override { visitor.visit(socket); }

        /** Convenience function that allows a template lambda to receive the socket reference */
        template<typename Handler>
        void with_socket(Handler handler)
        {
            handler(socket);
        }

    private:
        socket_type socket;
    };

    /** Create a socket reference in a shared pointer */
    inline auto make_socket_ref(boost::asio::ip::tcp::socket socket)
    {
        return std::make_shared<socket_reference_t<boost::asio::ip::tcp::socket>>(std::move(socket));
    }

    /** Create a socket reference in a shared pointer */
    inline auto make_socket_ref(boost::asio::local::stream_protocol::socket socket)
    {
        return std::make_shared<socket_reference_t<boost::asio::local::stream_protocol::socket>>(std::move(socket));
    }
}
