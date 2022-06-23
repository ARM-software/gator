/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "agents/agent_worker.h"
#include "agents/spawn_agent.h"
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
    protected:
        agent_worker_base_t(agent_process_t agent_process, state_change_observer_t && state_change_observer)
            : agent_process(std::move(agent_process)), state_change_observer(std::move(state_change_observer))
        {
            runtime_assert(this->agent_process.ipc_sink != nullptr, "missing ipc sink");
            runtime_assert(this->agent_process.ipc_source != nullptr, "missing ipc source");
        }

        /** @return True if the state transition from 'old_state' to 'new_state' is valid */
        static constexpr bool is_valid_state_transition(state_t old_state, state_t new_state)
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

                case state_t::terminated: {
                    return true;
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

            if (!is_valid_state_transition(old_state, new_state)) {
                LOG_DEBUG("Invalid transition from state # %d -> %d", int(old_state), int(new_state));
                return false;
            }

            LOG_DEBUG("Transitioning from state # %d -> %d", int(old_state), int(new_state));
            state = new_state;

            state_change_observer(agent_process.pid, old_state, new_state);

            return true;
        }

        [[nodiscard]] state_t get_state() const { return state; }
        [[nodiscard]] ipc::raw_ipc_channel_sink_t & sink() const { return *agent_process.ipc_sink; }
        [[nodiscard]] ipc::raw_ipc_channel_source_t & source() const { return *agent_process.ipc_source; }

    private:
        agent_process_t agent_process;
        state_change_observer_t state_change_observer;
        state_t state = state_t::launched;
    };
}
