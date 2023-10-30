/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#pragma once

#include "lib/Assert.h"

#include <cstddef>
#include <iterator>

#include <boost/asio/basic_socket_acceptor.hpp>
#include <boost/asio/basic_stream_socket.hpp>
#include <boost/asio/error.hpp>

#include <sys/socket.h>
#include <sys/un.h>

/**
 * The structures in this file implement a basic Asio endpoint & protocol for unix sockets. Asio already
 * provides an implementation in the boost::asio::local namespace that supports UDS, but it has a bug that
 * prevents us from creating paths with 108 chars in the abstract namespace. It forces a limit of 107
 * chars because it assumes it has to make room for a null termiantor, which isn't the case for abstract
 * paths.
 *
 */
namespace agents {

    template<typename ProtocolType>
    class uds_endpoint_t {
        /** size of the 'static' part (i.e. not dynamically sized) of the sockaddr_un struct */
        static constexpr size_t sockaddr_static_part_size = offsetof(sockaddr_un, sun_path);
        /** size of the dynamically sized part of the sockaddr_un struct */
        static constexpr size_t max_path_length = sizeof(sockaddr_un) - offsetof(sockaddr_un, sun_path);

    public:
        using protocol_type = ProtocolType;

        /** Default constructor - create an empty endpoint*/
        uds_endpoint_t() : path_length(0) {}

        /**
         * Construct an endpoint for a path specified by the string_view paramter.
         */
        uds_endpoint_t(std::string_view name) : path_length(name.size())
        {
            runtime_assert(name.size() <= max_path_length, "UDS path is longer than 108 chars");
            memcpy(socket.sun_path, name.data(), name.size());
        }

        /**
         * Construct an endpoint with the name copied from an arbitrary character buffer.
         * @tparam CharIterator
         * @param begin Iterator for the beginning of the character buffer.
         * @param end Iterator for the end of the character buffer.
         */
        template<typename CharIterator>
        uds_endpoint_t(CharIterator begin, CharIterator end)
        {
            auto size = std::distance(begin, end);
            runtime_assert(size >= 0, "Invalid iterator distance");
            path_length = static_cast<std::size_t>(size);
            runtime_assert(path_length <= max_path_length, "UDS path is longer than 108 chars");

            auto * out_it = std::begin(socket.sun_path);
            for (; begin != end; ++begin, ++out_it) {
                *out_it = *begin;
            }
        }

        uds_endpoint_t(const uds_endpoint_t<ProtocolType> & other)
            : socket(other.socket), path_length(other.path_length)
        {
        }

        uds_endpoint_t(uds_endpoint_t<ProtocolType> && other) noexcept
            : socket(other.socket), path_length(other.path_length)
        {
        }

        uds_endpoint_t<ProtocolType> & operator=(const uds_endpoint_t<ProtocolType> & other)
        {
            socket = other.socket;
            path_length = other.path_length;
        }

        uds_endpoint_t<ProtocolType> & operator=(uds_endpoint_t<ProtocolType> && other) noexcept
        {
            socket = other.socket;
            path_length = other.path_length;
        }

        [[nodiscard]] protocol_type protocol() const { return protocol_type(); }

        [[nodiscard]] const void * data() const { return &socket; }

        [[nodiscard]] void * data() { return &socket; }

        [[nodiscard]] std::size_t size() const { return path_length + sockaddr_static_part_size; }

        [[nodiscard]] std::size_t capacity() const { return sizeof(sockaddr_un); }

        void resize(std::size_t size)
        {
            if (size >= max_path_length || size < sockaddr_static_part_size) {
                const boost::system::error_code ec(boost::asio::error::invalid_argument);
                boost::asio::detail::throw_error(ec);
            }

            path_length = size - sockaddr_static_part_size;
        }

    private:
        sockaddr_un socket {AF_UNIX, {}};
        std::size_t path_length;
    };

    class uds_protocol_t {
    public:
        using acceptor = boost::asio::basic_socket_acceptor<uds_protocol_t>;

        using endpoint = uds_endpoint_t<uds_protocol_t>;

        using socket = boost::asio::basic_stream_socket<uds_protocol_t>;

        [[nodiscard]] static int family() { return AF_UNIX; }

        [[nodiscard]] static int type() { return SOCK_STREAM; }

        [[nodiscard]] static int protocol() { return 0; }
    };
}
