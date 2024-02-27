/* Copyright (C) 2022-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "async/completion_handler.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/operations.h"
#include "async/continuations/stored_continuation.h"
#include "async/continuations/use_continuation.h"
#include "lib/Assert.h"
#include "lib/EnumUtils.h"

#include <cstddef>
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

            return async::continuations::async_initiate_cont(
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
        auto async_receive_one(int cpu_no, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(event_t)>(
                [cpu_no, st = this->shared_from_this()](auto && stored_continuation) {
                    submit(start_on(st->strand) //
                               | then([cpu_no, st, r = stored_continuation.move()]() mutable {
                                     st->on_strand_do_receive_one(cpu_no, std::move(r));
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

                            // clear any state and cancel the pending requests if there are any
                            std::vector<per_core_state_t> pcs {std::move(st->per_core_states_list)};

                            for (auto & state : pcs) {
                                st->cancel_pending(std::move(state.pending_handler));
                            }
                        }));
        }

        bool is_safe_to_bring_online_or_offline(int cpu_no, bool online) const
        {
            //NOLINTNEXTLINE(readability-simplify-boolean-expr)
            runtime_assert(cpu_no >= 0, "Invalid cpu_no value");

            if ((cpu_no < 0) || (std::size_t(cpu_no) >= per_core_states_list.size())) {
                throw std::runtime_error("invalid state");
            }

            auto const & state = per_core_states_list[cpu_no];

            switch (state.current_state) {
                case state_t::initial_pending_online:
                case state_t::pending_offline_online:
                case state_t::pending_online:
                case state_t::online:
                    return online;
                case state_t::initial_pending_offline:
                case state_t::pending_online_offline:
                case state_t::pending_offline:
                case state_t::offline:
                    return !online;
                case state_t::initial_unknown:
                default:
                    throw std::runtime_error("invalid state_t");
            }
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

        struct per_core_state_t {
            completion_handler_t pending_handler {};
            state_t current_state = state_t::initial_unknown;
            bool transition_pending = false;
        };

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

        /** Find the next pending event */
        [[nodiscard]] static event_t find_next_pending_event(int cpu_no, per_core_state_t & state)
        {
            runtime_assert(state.transition_pending, "Unexpected call to find_next_pending_event");

            // transform its state
            auto const current_state = state.current_state;

            runtime_assert(is_pending(current_state), "Unexpected core state");

            auto const [new_state, online] = consume_pending(current_state);

            state.current_state = new_state;

            LOG_TRACE("Consuming coalesced CPU state from %u->%u, %u / %u",
                      lib::toEnumValue(current_state),
                      lib::toEnumValue(new_state),
                      online,
                      is_pending(new_state));

            // if it is no longer pending, remove the core, otherwise leave at the front for next time
            state.transition_pending = is_pending(new_state);

            return {cpu_no, online};
        }

        boost::asio::io_context::strand strand;
        std::vector<per_core_state_t> per_core_states_list {};
        bool terminated {false};

        /** Trigger the handler asynchronously */
        template<typename Handler>
        void post_handler(Handler && handler, event_t event)
        {
            resume_continuation(strand.context(), std::forward<Handler>(handler), event);
        }

        /** Cancel and clear any pending request */
        void cancel_pending(completion_handler_t && pending_handler)
        {
            completion_handler_t prev_pending {std::move(pending_handler)};

            if (prev_pending) {
                // call it with invalid cpu
                post_handler(std::move(prev_pending), event_t {-1, false});
            }
        }

        [[nodiscard]] per_core_state_t & get_or_create_per_core_state(int cpu_no)
        {
            runtime_assert(cpu_no >= 0, "Invalid cpu no received");

            std::size_t const ndx = cpu_no;

            if (ndx >= per_core_states_list.size()) {
                per_core_states_list.resize(ndx + 1);
            }

            return per_core_states_list[ndx];
        }

        /** Handle the request to consume one pending event */
        template<typename Handler>
        void on_strand_do_receive_one(int cpu_no, Handler && handler)
        {
            // get or insert the current state of the core
            auto & state = get_or_create_per_core_state(cpu_no);

            // is there already a pending handler? cancel it
            cancel_pending(std::move(state.pending_handler));

            // newly inserted OR no pending events, just store handler
            if (!state.transition_pending) {
                // cancel the new request if terminated
                if (terminated) {
                    // call it with invalid cpu
                    post_handler(std::forward<Handler>(handler), event_t {-1, false});
                }
                else {
                    // no, store the new handler and exit
                    state.pending_handler = completion_handler_t {std::forward<Handler>(handler)};
                }
                return;
            }

            // yes, find the next pending item for any subsequent call, then post the handler
            post_handler(std::forward<Handler>(handler), find_next_pending_event(cpu_no, state));
        }

        /** Update state from new raw event */
        void on_strand_on_raw_event(int cpu_no, bool online)
        {
            // ignore the call if shutdown
            if (terminated) {
                return;
            }

            // make sure the vector has the core's index
            auto & state = get_or_create_per_core_state(cpu_no);

            // calculate transition
            auto const current_state = state.current_state;
            auto const new_state = transition(current_state, online);
            auto const was_pending = is_pending(current_state);
            auto const now_pending = is_pending(new_state);

            LOG_TRACE("Transitioning coalesced CPU state from %u->%u (%u/%u)",
                      lib::toEnumValue(current_state),
                      lib::toEnumValue(new_state),
                      was_pending,
                      now_pending);

            // update state
            state.current_state = new_state;

            // handle transition to pending
            if (now_pending && !was_pending) {
                // mark as having a transition pending
                state.transition_pending = true;

                // is there a handler waiting ?
                completion_handler_t prev_pending {std::move(state.pending_handler)};
                if (prev_pending) {
                    // call it by post
                    post_handler(std::move(prev_pending), find_next_pending_event(cpu_no, state));
                }
            }
        }
    };
}
