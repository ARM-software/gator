/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include <boost/asio/basic_datagram_socket.hpp>

#include <linux/netlink.h>
#include <sys/socket.h>

namespace async::netlink {

    template<int Protocol>
    class netlink_endpoint_t;

    /** Asio protocol type for AF_NETLINK sockets */
    template<int Protocol>
    class netlink_protocol_t {
    public:
        using endpoint = netlink_endpoint_t<Protocol>;
        using socket = boost::asio::basic_datagram_socket<netlink_protocol_t>;

        constexpr netlink_protocol_t() = default;

        [[nodiscard]] static int family() { return AF_NETLINK; }

        [[nodiscard]] int protocol() const { return Protocol; }

        [[nodiscard]] static int type() { return SOCK_DGRAM; }
    };

    /** Asio endpoint type for AF_NETLINK sockets */
    template<int Protocol>
    class netlink_endpoint_t {
    public:
        using protocol_type = netlink_protocol_t<Protocol>;
        using data_type = boost::asio::detail::socket_addr_type;

        explicit constexpr netlink_endpoint_t(std::uint32_t groups = 0, std::uint32_t pid = 0) : address()
        {
            address.nl_family = AF_NETLINK;
            address.nl_pid = pid;
            address.nl_groups = groups;
        }

        [[nodiscard]] static protocol_type protocol() { return {}; }

        [[nodiscard]] data_type * data() { return reinterpret_cast<data_type *>(&address); }

        [[nodiscard]] data_type const * data() const { return reinterpret_cast<data_type const *>(&address); }

        [[nodiscard]] constexpr std::size_t size() const { return sizeof(address); }

        [[nodiscard]] constexpr std::size_t capacity() const { return sizeof(address); }

    private:
        sockaddr_nl address;
    };

}
