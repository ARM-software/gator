/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/async_initiate.h"
#include "async/continuations/stored_continuation.h"
#include "async/netlink/uevents.h"
#include "lib/String.h"

#include <memory>
#include <string>
#include <string_view>

#include <boost/asio/io_context.hpp>
#include <boost/system/error_code.hpp>

namespace agents {
    /**
     * Monitors uevents for cpu online and offline events and generates the relevent async event once one is received
     *
     * @tparam Monitor The uevent monitor type (for unit testing)
     */
    template<typename Monitor = async::netlink::nl_kobject_uevent_monitor_t<>>
    class nl_kobject_uevent_cpu_monitor_t
        : public std::enable_shared_from_this<nl_kobject_uevent_cpu_monitor_t<Monitor>> {
    public:
        using monitor_type = Monitor;

        /** One CPU state change value */
        struct event_t {
            int cpu_no;
            bool online;
        };

        /** Constructor, using the provided context */
        explicit nl_kobject_uevent_cpu_monitor_t(boost::asio::io_context & context) : context(context), monitor(context)
        {
        }

        /** Constructor, using the provided monitor (for testing) */
        explicit nl_kobject_uevent_cpu_monitor_t(boost::asio::io_context & context, monitor_type && monitor)
            : context(context), monitor(std::forward<monitor_type>(monitor))
        {
        }

        /** @return True if the socket is open, false otherwise */
        [[nodiscard]] bool is_open() const { return monitor.is_open(); }

        /** Stop observing for changes */
        void stop() { monitor.stop(); }

        /**
         * Receive one parsed event, which will be the error code, plus event containing cpu number and online/offline flag
         */
        template<typename CompletionToken>
        auto async_receive_one(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(event_t)>(
                [st = this->shared_from_this()](auto && sc) { st->do_receive_event(std::forward<decltype(sc)>(sc)); },
                std::forward<CompletionToken>(token));
        }

    private:
        static constexpr std::string_view action_online {"online"};
        static constexpr std::string_view action_offline {"offline"};
        static constexpr std::string_view devpath_cpu_prefix {"/devices/system/cpu/cpu"};
        static constexpr std::string_view subsystem_cpu {"cpu"};

        boost::asio::io_context & context;
        monitor_type monitor;

        /** Async wait for one uevent to be received and parsed */
        template<typename R, typename E>
        void do_receive_event(async::continuations::raw_stored_continuation_t<R, E, event_t> && sc)
        {
            monitor.async_receive_one(
                [st = this->shared_from_this(), sc = std::move(sc)](auto const & ec, auto const & event) mutable {
                    if (!ec) {
                        st->process_event(std::move(sc), event);
                    }
                    else {
                        // convert it into stop code
                        LOG_DEBUG("Received '%s', stopping Netlink CPU monitor", ec.message().c_str());
                        resume_continuation(st->context, std::move(sc), event_t {-1, false});
                    }
                });
        }

        /** Parse the received event; will recurse for another event if the event is not a cpu online/offline event, otherwise passes to the handler */
        template<typename R, typename E, typename Event>
        void process_event(async::continuations::raw_stored_continuation_t<R, E, event_t> && sc, Event const & event)
        {
            if (event.subsystem != subsystem_cpu) {
                return do_receive_event(std::move(sc));
            }

            if (!lib::starts_with(event.devpath, devpath_cpu_prefix)) {
                return do_receive_event(std::move(sc));
            }

            auto online = (event.action == action_online);
            auto offline = (event.action == action_offline);

            if ((!online) && (!offline)) {
                return do_receive_event(std::move(sc));
            }

            auto cpu_no_sv = event.devpath.substr(devpath_cpu_prefix.size());
            auto cpu_no = lib::to_int<int>(cpu_no_sv, -1);
            if (cpu_no < 0) {
                return do_receive_event(std::move(sc));
            }

            // dispatch the handler with the event
            resume_continuation(context, std::move(sc), event_t {cpu_no, online});
        }
    };
}
