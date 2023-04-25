/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "async/completion_handler.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/operations.h"
#include "async/continuations/stored_continuation.h"
#include "async/continuations/use_continuation.h"
#include "lib/Assert.h"
#include "lib/EnumUtils.h"

#include <deque>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <boost/asio/async_result.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/io_context_strand.hpp>
#include <boost/system/error_code.hpp>

namespace agents {
    /**
     * CPU online/offline events are monitored and in theory could happen quite often. The online/offline of per-cpu perf counters can take some time.
     * This class coalesces multiple on-off events so that only the final before-after state is seen from the PoV of the external event consumer.
     */
    class coalescing_cpu_monitor_t : public std::enable_shared_from_this<coalescing_cpu_monitor_t> {
    public:
        struct event_t {
            int cpu_no;
            bool online;
        };

        /** Constructor, using the provided context */
        explicit coalescing_cpu_monitor_t(boost::asio::io_context & context) : strand(context) {}

        /** Insert a new event into the monitor */
        template<typename CompletionToken>
        auto async_update_state(int cpu_no, bool online, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate(
                [st = shared_from_this(), cpu_no, online]() {
                    return start_on(st->strand)                      //
                         | do_if([cpu_no]() { return cpu_no >= 0; }, // ignore invalid cpu_no values
                                 [st, cpu_no, online]() { st->on_strand_on_raw_event(cpu_no, online); });
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Receive an event from the monitor
         */
        template<typename CompletionToken>
        auto async_receive_one(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(event_t)>(
                [st = this->shared_from_this()](auto && stored_continuation) {
                    submit(start_on(st->strand) //
                               | then([st, r = stored_continuation.move()]() mutable {
                                     st->on_strand_do_receive_one(std::move(r));
                                 }),
                           stored_continuation.get_exceptionally());
                },
                std::forward<CompletionToken>(token));
        }

        /** Stop the monitor and cancel any pending receive_one's */
        void stop()
        {
            using namespace async::continuations;

            spawn("terminate cpu monitor",
                  start_on(strand) //
                      | then([st = shared_from_this()]() {
                            // mark as terminated
                            st->terminated = true;
                            // cancel the pending request if there is one
                            st->cancel_pending();
                            // clear any state
                            st->per_core_state.clear();
                            st->pending_cpu_nos.clear();
                        }));
        }

    private:
        enum class state_t {
            initial_unknown,
            initial_pending_offline,
            initial_pending_online,
            offline,
            online,
            pending_offline,
            pending_online,
            pending_offline_online,
            pending_online_offline,
        };

        using completion_handler_t = async::continuations::stored_continuation_t<event_t>;

        /**
         * Transition current->new state value based on received raw on-off event
         *
         * @param current_state The current state
         * @param online True if the new raw event was online, false if offline event
         * @return The new state
         */
        static constexpr state_t transition(state_t current_state, bool online)
        {
            switch (current_state) {
                case state_t::initial_unknown:
                case state_t::initial_pending_offline:
                case state_t::initial_pending_online:
                    return (online ? state_t::initial_pending_online : state_t::initial_pending_offline);
                case state_t::online:
                    return (online ? state_t::online : state_t::pending_offline);
                case state_t::offline:
                    return (online ? state_t::pending_online : state_t::offline);
                case state_t::pending_online:
                    return (online ? state_t::pending_online : state_t::pending_online_offline);
                case state_t::pending_offline:
                    return (online ? state_t::pending_offline_online : state_t::pending_offline);
                case state_t::pending_online_offline:
                    return (online ? state_t::pending_online : state_t::pending_online_offline);
                case state_t::pending_offline_online:
                    return (online ? state_t::pending_offline_online : state_t::pending_offline);
                default:
                    throw std::runtime_error("unexpected state_t");
            }
        }

        /** @return true for pending states (where an event will be generated to the consumer), false otherwise */
        static constexpr bool is_pending(state_t state)
        {
            switch (state) {
                case state_t::initial_unknown:
                case state_t::online:
                case state_t::offline:
                    return false;
                case state_t::initial_pending_offline:
                case state_t::initial_pending_online:
                case state_t::pending_online:
                case state_t::pending_offline:
                case state_t::pending_online_offline:
                case state_t::pending_offline_online:
                    return true;
                default:
                    throw std::runtime_error("unexpected state_t");
            }
        }

        /**
         * Returns the new state and online/offline value to send to the consumer when consuming the current pending state.
         *
         * Only valid when `is_pending(current_state)` returns true.
         *
         * @param current_state The current state
         * @return A pair of (new state, online), where `new state` is the new state to track in this object, and online is the event state to pass to the consumer
         */
        static constexpr std::pair<state_t, bool> consume_pending(state_t current_state)
        {
            switch (current_state) {
                case state_t::initial_pending_online:
                case state_t::pending_online:
                    return {state_t::online, true};
                case state_t::initial_pending_offline:
                case state_t::pending_offline:
                    return {state_t::offline, false};
                case state_t::pending_online_offline:
                    return {state_t::pending_offline, true};
                case state_t::pending_offline_online:
                    return {state_t::pending_online, false};

                case state_t::online:
                case state_t::offline:
                    throw std::runtime_error("not valid pending state_t");
                default:
                    throw std::runtime_error("unexpected state_t");
            }
        }

        boost::asio::io_context::strand strand;
        std::vector<state_t> per_core_state {};
        completion_handler_t pending_handler {};
        std::deque<int> pending_cpu_nos {};
        bool terminated {false};

        /** Trigger the handler asynchronously */
        template<typename Handler>
        void post_handler(Handler && handler, event_t event)
        {
            resume_continuation(strand.context(), std::forward<Handler>(handler), event);
        }

        /** Cancel and clear any pending request */
        void cancel_pending()
        {
            completion_handler_t prev_pending {std::move(pending_handler)};
            if (prev_pending) {
                // call it with invalid cpu
                post_handler(std::move(prev_pending), event_t {-1, false});
            }
        }

        /** Handle the request to consume one pending event */
        template<typename Handler>
        void on_strand_do_receive_one(Handler && handler)
        {
            // cancel the pending request if there is one
            cancel_pending();

            // is there anything already pending
            if (pending_cpu_nos.empty()) {
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
            post_handler(std::forward<Handler>(handler), find_next_pending_event());
        }

        /** Find the next pending event */
        event_t find_next_pending_event()
        {
            runtime_assert(!pending_cpu_nos.empty(), "Unexpected call to find_next_pending_event");

            // find the next pending core
            auto cpu_no = pending_cpu_nos.front();

            runtime_assert((cpu_no >= 0) && (std::size_t(cpu_no) < per_core_state.size()), "Invalid cpu_no value");

            // transform its state
            auto current_state = per_core_state[cpu_no];

            runtime_assert(is_pending(current_state), "Unexpected core state");

            auto [new_state, online] = consume_pending(current_state);

            per_core_state[cpu_no] = new_state;

            LOG_TRACE("Consuming coalesced CPU state from %u->%u, %u / %u",
                      lib::toEnumValue(current_state),
                      lib::toEnumValue(new_state),
                      online,
                      is_pending(new_state));

            // if it is no longer pending, remove the core, otherwise leave at the front for next time
            if (!is_pending(new_state)) {
                pending_cpu_nos.pop_front();
            }

            return {cpu_no, online};
        }

        /** Update state from new raw event */
        void on_strand_on_raw_event(int cpu_no, bool online)
        {
            runtime_assert(cpu_no >= 0, "Invalid cpu no received");

            // ignore the call if shutdown
            if (terminated) {
                return;
            }

            auto index = std::size_t(cpu_no);

            // make sure the vector has the core's index
            if (index >= per_core_state.size()) {
                per_core_state.resize(index + 1);
            }

            // calculate transition
            auto current_state = per_core_state[index];
            auto new_state = transition(current_state, online);
            auto was_pending = is_pending(current_state);
            auto now_pending = is_pending(new_state);

            LOG_TRACE("Transitioning coalesced CPU state from %u->%u (%u/%u)",
                      lib::toEnumValue(current_state),
                      lib::toEnumValue(new_state),
                      was_pending,
                      now_pending);

            // update state
            per_core_state[index] = new_state;

            // handle transition to pending
            if (now_pending && !was_pending) {
                // is there a handler waiting ?
                completion_handler_t prev_pending {std::move(pending_handler)};
                if (prev_pending) {
                    // call it by post
                    post_handler(std::move(prev_pending), event_t {cpu_no, online});
                }
                else {
                    // queue it up
                    pending_cpu_nos.push_back(cpu_no);
                }
            }
        }
    };
}
