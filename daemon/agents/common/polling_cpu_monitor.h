/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/completion_handler.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "lib/Assert.h"
#include "lib/FsEntry.h"

#include <chrono>
#include <deque>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include <boost/asio/async_result.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/io_context_strand.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

namespace agents {
    /**
     * Monitors CPU online state by polling one or more files in sysfs (specifically the /sys/devices/system/cpu<n>/online)
     */
    class polling_cpu_monitor_t : public std::enable_shared_from_this<polling_cpu_monitor_t> {
    public:
        struct event_t {
            int cpu_no;
            bool online;
        };

        /**
         * Helper method finds all the cpu<n>/online sysfs paths
         *
         * @return A vector of tuples of (path, no) for the path to the 'online' file and the decoded cpu number
         */
        static std::vector<std::pair<lib::FsEntry, int>> find_all_cpu_paths()
        {
            const lib::FsEntry sys_fs_cpu_root_path = lib::FsEntry::create("/sys/devices/system/cpu");

            std::vector<std::pair<lib::FsEntry, int>> result {};
            std::optional<lib::FsEntry> child;
            lib::FsEntryDirectoryIterator iterator = sys_fs_cpu_root_path.children();

            while (!!(child = iterator.next())) {
                const auto & name = child->name();
                if ((name.length() > 3) && (name.rfind("cpu", 0) == 0)) {
                    const auto cpu = strtol(name.c_str() + 3, nullptr, 10);

                    result.emplace_back(lib::FsEntry::create(*child, "online"), cpu);
                }
            }

            return result;
        }

        /** Constructor, using the provided context */
        static std::shared_ptr<polling_cpu_monitor_t> create(
            boost::asio::io_context & context,
            std::vector<std::pair<lib::FsEntry, int>> monitor_paths = find_all_cpu_paths())
        {
            return std::make_shared<polling_cpu_monitor_t>(context, std::move(monitor_paths));
        }

        /** Constructor, using the provided context */
        explicit polling_cpu_monitor_t(boost::asio::io_context & context,
                                       std::vector<std::pair<lib::FsEntry, int>> monitor_paths = find_all_cpu_paths())
            : timer(context), strand(context), monitor_paths(std::move(monitor_paths))
        {
        }

        /** Start observing for changes */
        void start()
        {
            using namespace async::continuations;
            using namespace std::chrono_literals;

            // short interval to catch case where core onlines
            static constexpr auto short_poll_interval = 200us;
            // longer interval for when all cores are on and we assume they are likely to stay on (or it doesnt matter if they go offline and we miss the event slightly)
            static constexpr auto long_poll_interval = 1000us;

            auto st = shared_from_this();

            repeatedly(
                [st]() {
                    return start_on(st->strand) //
                         | then([st]() { return (!st->terminated) || (!st->monitor_paths.empty()); });
                },
                [st]() {
                    return start_on(st->strand)                             //
                         | then([st]() { return st->on_strand_do_poll(); }) //
                         | then([st](bool any_offline) {
                               st->timer.expires_from_now(any_offline ? short_poll_interval : long_poll_interval);
                           })                                     //
                         | st->timer.async_wait(use_continuation) //
                         | dispatch_on(st->strand)                //
                         | then([st](auto ec) {
                               // swallow cancel event, mark as terminated instead
                               if (ec == boost::asio::error::make_error_code(boost::asio::error::operation_aborted)) {
                                   LOG_DEBUG("Polling CPU monitor is now terminated");
                                   if (!std::exchange(st->terminated, true)) {
                                       st->enqueue_event(-1, false);
                                   }
                                   return boost::system::error_code {};
                               }
                               if (ec) {
                                   LOG_ERROR("??? %s", ec.message().c_str());
                               }
                               return ec;
                           }) //
                         | map_error();
                }) //
                | DETACH_LOG_ERROR("raw cpu event monitor");
        }

        /** Stop observing for changes */
        void stop()
        {
            using namespace async::continuations;

            auto st = shared_from_this();

            start_on(strand) //
                | then([st]() {
                      if (!std::exchange(st->terminated, true)) {
                          st->timer.cancel();
                          st->enqueue_event(-1, false);
                      }
                  }) //
                | DETACH_LOG_ERROR("stop raw cpu event monitor");
        }

        template<typename CompletionToken>
        auto async_receive_one(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(event_t)>(
                [st = shared_from_this()](auto && receiver, auto && exceptionally) {
                    submit(start_on(st->strand) //
                               | then([st, r = std::forward<decltype(receiver)>(receiver)]() mutable {
                                     st->on_strand_do_receive_one(std::move(r));
                                 }),
                           std::forward<decltype(exceptionally)>(exceptionally));
                },
                std::forward<CompletionToken>(token));
        }

    private:
        using completion_handler_t = async::completion_handler_ref_t<event_t>;

        boost::asio::steady_timer timer;
        boost::asio::io_context::strand strand;
        std::vector<std::pair<lib::FsEntry, int>> monitor_paths;
        completion_handler_t pending_handler {};
        std::set<unsigned> online_cpu_nos {};
        std::deque<event_t> pending_events {};
        bool terminated {false};
        bool first_pass {true};

        /** Trigger the handler asynchronously */
        template<typename Handler>
        void post_handler(Handler && handler, event_t event)
        {
            boost::asio::post(strand.context(), [event, h = std::forward<Handler>(handler)]() mutable { h(event); });
        }

        /** Handle the request to consume one pending event */
        template<typename Handler>
        void on_strand_do_receive_one(Handler && handler)
        {
            // cancel the pending request if there is one
            completion_handler_t prev_pending {std::move(pending_handler)};
            if (prev_pending) {
                // call it with invalid cpu
                post_handler(std::move(prev_pending), event_t {-1, false});
            }

            // is there anything already pending
            if (pending_events.empty()) {
                // cancel the new request if terminated
                if (terminated) {
                    // call it with invalid cpu
                    post_handler(std::forward<Handler>(handler), event_t {-1, false});
                }
                else {
                    // no, store the new handler and exit
                    pending_handler = {std::forward<Handler>(handler)};
                }
                return;
            }

            // yes, find the next pending item for any subsequent call, then post the handler
            post_handler(std::forward<Handler>(handler), get_next_pending_event());
        }

        /** @return the next pending event */
        [[nodiscard]] event_t get_next_pending_event()
        {
            runtime_assert(!pending_events.empty(), "Unexpected call to get_next_pending_event");

            auto event = pending_events.front();
            pending_events.pop_front();

            return event;
        }

        /** Check for some state change, return true if any are offline */
        [[nodiscard]] bool on_strand_do_poll()
        {
            if (terminated) {
                return false;
            }

            bool any_offline = false;

            for (auto const & entry : monitor_paths) {
                const std::string contents = entry.first.readFileContentsSingleLine();
                if (!contents.empty()) {
                    const unsigned online_value = strtoul(contents.c_str(), nullptr, 0);
                    const bool is_online = (online_value != 0);
                    any_offline |= !is_online;

                    // process it
                    process_one(entry.second, is_online);
                }
            }

            // not first pass any more
            first_pass = false;

            return any_offline;
        }

        /** Process one polled value */
        void process_one(int cpu, bool online)
        {
            if (online) {
                auto [it, inserted] = online_cpu_nos.insert(cpu);
                if (inserted || first_pass) {
                    enqueue_event(cpu, true);
                }
            }
            else {
                auto count = online_cpu_nos.erase(cpu);
                if ((count > 0) || first_pass) {
                    enqueue_event(cpu, false);
                }
            }
        }

        /** Emit one event */
        void enqueue_event(int cpu, bool online)
        {
            // is there a handler waiting ?
            completion_handler_t prev_pending {std::move(pending_handler)};
            if (prev_pending) {
                // call it by post
                post_handler(std::move(prev_pending), event_t {cpu, online});
            }
            else {
                // queue it up
                pending_events.push_back(event_t {cpu, online});
            }
        }
    };
}
