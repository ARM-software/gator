/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/stored_continuation.h"
#include "async/continuations/use_continuation.h"
#include "async/netlink/nl_protocol.h"
#include "lib/String.h"

#include <array>
#include <cstring>
#include <string_view>

#include <boost/asio/async_result.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/system/error_code.hpp>

#include <linux/netlink.h>

namespace async::netlink {

    using nl_kobject_uevent_protocol_t = netlink_protocol_t<NETLINK_KOBJECT_UEVENT>;

    /**
     * Wrapper around a NETLINK_KOBJECT_UEVENT socket that allows receiving single datagrams from the socket, one event at a time
     *
     * @tparam BufferSize The datagram buffer size (must be larger than the maximum datagram)
     */
    template<std::size_t BufferSize = 8192>
    class nl_kobject_uevent_socket_t {
    public:
        static constexpr int group_kernel = 1;
        static constexpr int group_udev = 2;

        using protocol_type = nl_kobject_uevent_protocol_t;
        using endpoint_type = typename protocol_type::endpoint;
        using socket_type = typename protocol_type::socket;

        /** Construct, possibly configuring the endpoint */
        explicit nl_kobject_uevent_socket_t(boost::asio::io_context & context,
                                            endpoint_type const & endpoint = endpoint_type {group_kernel})
            : socket(context)
        {
            // use the error checking rather than throwing methods so that we can report 'closed' state instead of throwing as use of netlink is optional and not supported on android unless root
            boost::system::error_code ec {};

            socket.open(protocol_type(), ec);
            if (!!ec) {
                socket.close();
            }

            socket.bind(endpoint, ec);
            if (!!ec) {
                socket.close(ec);
            }
        }

        /** @return True if the socket is open, false otherwise */
        [[nodiscard]] bool is_open() const { return socket.is_open(); }

        /** Close the socket */
        void close() { socket.close(); }

        /**
         * Receive one whole datagram, which will be passed to the completion token as a string_view.
         *
         * Do not call this funtion multiple times on the same object without first having the completion
         * token called.
         */
        template<typename CompletionToken>
        auto async_receive_one(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(boost::system::error_code, std::string_view)>(
                [this](auto && sc) {
                    submit(socket.async_receive(boost::asio::buffer(buffer),
                                                use_continuation) //
                               | then([this](auto const & ec, auto n) {
                                     return start_with(ec, std::string_view(buffer.data(), !ec ? n : 0));
                                 }),
                           std::forward<decltype(sc)>(sc));
                },
                std::forward<CompletionToken>(token));
        }

    private:
        socket_type socket;
        std::array<char, BufferSize> buffer;
    };

    /**
     * A simple uevent parser, receives uevents from a netlink socket, and for each event parses out the action, devpath and subsystem fields.
     *
     * @tparam Socket The socket type (provided for unit testing only)
     */
    template<typename Socket = nl_kobject_uevent_socket_t<>>
    class nl_kobject_uevent_monitor_t {
    public:
        using socket_type = Socket;

        /** One kobject uevent value */
        struct event_t {
            /** the ACTION= value */
            std::string_view action;
            /** the DEVPATH= value */
            std::string_view devpath;
            /** the SUBSYSTEM= value */
            std::string_view subsystem;
        };

        /** Constructor, using the provided context */
        explicit nl_kobject_uevent_monitor_t(boost::asio::io_context & context) : context(context), socket(context) {}

        /** Constructor, using the provided socket (for testing) */
        nl_kobject_uevent_monitor_t(boost::asio::io_context & context, socket_type && socket)
            : context(context), socket(std::forward<socket_type>(socket))
        {
        }

        /** @return True if the socket is open, false otherwise */
        [[nodiscard]] bool is_open() const { return socket.is_open(); }

        /** Stop observing for changes */
        void stop() { socket.close(); }

        /**
         * Receive one parsed event, which will be the error code, plus event containing ACTION= string, DEVPATH= string and SUBSYSTEM= strings
         * parsed from the raw uevent.
         */
        template<typename CompletionToken>
        auto async_receive_one(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(boost::system::error_code, event_t)>(
                [this](auto && sc) { this->do_receive_one(std::forward<decltype(sc)>(sc)); },
                std::forward<CompletionToken>(token));
        }

    private:
        static constexpr std::string_view action_prefix {"ACTION="};
        static constexpr std::string_view devpath_prefix {"DEVPATH="};
        static constexpr std::string_view subsystem_prefix {"SUBSYSTEM="};

        boost::asio::io_context & context;
        socket_type socket;

        template<typename R, typename E>
        void do_receive_one(
            async::continuations::raw_stored_continuation_t<R, E, boost::system::error_code, event_t> && sc)
        {
            LOG_TRACE("Waiting for uevent data");

            socket.async_receive_one([this, sc = std::forward<decltype(sc)>(sc)](auto const & ec, auto sv) mutable {
                if (!ec) {
                    this->parse(std::move(sc), sv);
                }
                else {
                    LOG_ERROR_IF_NOT_EOF_OR_CANCELLED(ec,
                                                      "Unexpected NETLINK_KOBJECT_UEVENT socket error %s",
                                                      ec.message().c_str());

                    resume_continuation(context, std::move(sc), ec, event_t {});
                }
            });
        }

        /**
         * Each netlink datagram is an sequence of null-terminated cstrings.
         * This method extracts the relevent strings from that sequence and
         * passes them to the handler as an `event_t` object.
         *
         * @param sc The stored continuation that receives the event
         * @param sv The string view containing the raw event blob
         */
        template<typename R, typename E>
        void parse(async::continuations::raw_stored_continuation_t<R, E, boost::system::error_code, event_t> && sc,
                   std::string_view sv)
        {
            bool has_action = false;
            bool has_devpath = false;
            bool has_subsystem = false;
            event_t event {};

            LOG_TRACE("uevent received");

            // split out the relevent parts
            while ((!sv.empty()) && !(has_action && has_devpath && has_subsystem)) {
                // find the null terminator, which marks the end of the substring
                auto l = strnlen(sv.data(), sv.size());
                auto s = std::string_view(sv.data(), l);

                LOG_TRACE("uevent field - '%s'", s.data());

                if (lib::starts_with(s, action_prefix)) {
                    event.action = s.substr(action_prefix.size());
                    has_action = true;
                }

                if (lib::starts_with(s, devpath_prefix)) {
                    event.devpath = s.substr(devpath_prefix.size());
                    has_devpath = true;
                }

                if (lib::starts_with(s, subsystem_prefix)) {
                    event.subsystem = s.substr(subsystem_prefix.size());
                    has_subsystem = true;
                }

                // iterate next substring
                auto f = l + 1;

                sv = (f < sv.size() ? sv.substr(f) : std::string_view {});
            }

            // consume the event ?
            if (has_action && has_devpath && has_subsystem) {
                LOG_TRACE("has valid uevent '%s', '%s', '%s'",
                          event.action.data(),
                          event.devpath.data(),
                          event.subsystem.data());
                return resume_continuation(context, std::move(sc), boost::system::error_code {}, event);
            }

            // ignore the event if the string_view is not parsed correctly
            return do_receive_one(std::move(sc));
        }
    };
}
