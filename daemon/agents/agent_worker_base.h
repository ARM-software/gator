/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "agents/agent_worker.h"
#include "agents/spawn_agent.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/stored_continuation.h"
#include "ipc/raw_ipc_channel_sink.h"
#include "lib/Assert.h"

#include <functional>
#include <memory>
#include <stdexcept>

namespace agents {

    /**
     * Common base class for agent worker classes, implementing the agent worker interface and providing basic functionality such as state tracking and access to the IPC mechanism
     */
    class agent_worker_base_t : public i_agent_worker_t {
    public:
        template<typename CompletionToken>
        auto async_wait_launched(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(bool)>(
                [this](auto && sc) {
                    auto & strand = work_strand();

                    submit(start_on(strand) //
                               | then([this, sc = sc.move()]() mutable {
                                     runtime_assert((!launched_notification) && (!notified_launched),
                                                    "cannot queue multiple launch notifications");

                                     // if the state has already changed, just call directly
                                     if (state != state_t::launched) {
                                         notified_launched = true;
                                         resume_continuation(work_strand().context(),
                                                             std::move(sc),
                                                             state == state_t::ready);
                                     }
                                     // store it
                                     else {
                                         launched_notification = std::move(sc);
                                     }
                                 }),
                           sc.get_exceptionally());
                },
                std::forward<CompletionToken>(token));
        }

    protected:
        agent_worker_base_t(agent_process_t agent_process, state_change_observer_t && state_change_observer)
            : agent_process(std::move(agent_process)), state_change_observer(std::move(state_change_observer))
        {
            runtime_assert(this->agent_process.ipc_sink != nullptr, "missing ipc sink");
            runtime_assert(this->agent_process.ipc_source != nullptr, "missing ipc source");
        }

        /** @return True if the state transition from 'old_state' to 'new_state' is valid */
        static constexpr bool is_valid_state_transition(state_t old_state,
                                                        state_t new_state,
                                                        bool message_loop_terminated)
        {
            if (old_state == new_state) {
                return false;
            }

            switch (new_state) {
                case state_t::launched: {
                    return false;
                }

                case state_t::ready: {
                    return old_state == state_t::launched;
                }

                case state_t::shutdown_requested: {
                    return ((old_state == state_t::launched) || (old_state == state_t::ready));
                }

                case state_t::shutdown_received: {
                    return ((old_state == state_t::launched) || (old_state == state_t::ready)
                            || (old_state == state_t::shutdown_requested));
                }

                case state_t::terminated_pending_message_loop: {
                    return !message_loop_terminated;
                }

                case state_t::terminated: {
                    return message_loop_terminated;
                }

                default: {
                    throw std::runtime_error("Unexpected state_t value");
                }
            }
        }

        /** Process state transition to new state */
        bool transition_state(state_t new_state)
        {
            const auto old_state = state;

            // fix up terminated state transition which is dependent on  message_loop_terminated value
            if ((new_state == state_t::terminated_pending_message_loop) && message_loop_terminated) {
                new_state = state_t::terminated;
            }
            else if ((new_state == state_t::terminated) && !message_loop_terminated) {
                new_state = state_t::terminated_pending_message_loop;
            }

            if (!is_valid_state_transition(old_state, new_state, message_loop_terminated)) {
                LOG_DEBUG("Invalid transition from state # %d -> %d", int(old_state), int(new_state));
                return false;
            }

            LOG_DEBUG("Transitioning from state # %d -> %d", int(old_state), int(new_state));
            state = new_state;

            state_change_observer(agent_process.forked_process.get_pid(), old_state, new_state);

            // notify the listener
            launched_notification_t launched_notification {std::move(this->launched_notification)};
            if (launched_notification) {
                notified_launched = true;
                resume_continuation(work_strand().context(),
                                    std::move(launched_notification),
                                    new_state == state_t::ready);
            }

            return true;
        }

        // subclasses must provide
        [[nodiscard]] virtual boost::asio::io_context::strand & work_strand() = 0;

        [[nodiscard]] state_t get_state() const { return state; }
        [[nodiscard]] ipc::raw_ipc_channel_sink_t & sink() const { return *agent_process.ipc_sink; }
        [[nodiscard]] ipc::raw_ipc_channel_source_t & source() const { return *agent_process.ipc_source; }
        [[nodiscard]] std::shared_ptr<ipc::raw_ipc_channel_source_t> source_shared() const
        {
            return agent_process.ipc_source;
        }

        [[nodiscard]] bool exec_agent() { return agent_process.forked_process.exec(); }

        void set_message_loop_terminated()
        {
            message_loop_terminated = true;
            if (state == state_t::terminated_pending_message_loop) {
                transition_state(state_t::terminated);
            }
        }

        virtual void async_send_message(
            ipc::all_message_types_variant_t message,
            boost::asio::io_context & io_context,
            async::continuations::stored_continuation_t<boost::system::error_code> sc) override
        {
            std::visit(
                [&](auto && msg) {
                    sink().async_send_message( //
                        std::move(msg),
                        [&io_context, sc = std::move(sc)](const auto & ec, const auto & /*msg*/) mutable {
                            resume_continuation(io_context, std::move(sc), ec);
                        });
                },
                std::move(message));
        }

    private:
        using launched_notification_t = async::continuations::stored_continuation_t<bool>;

        agent_process_t agent_process;
        state_change_observer_t state_change_observer;
        launched_notification_t launched_notification;
        state_t state = state_t::launched;
        bool notified_launched {false};
        bool message_loop_terminated {false};
    };
}
