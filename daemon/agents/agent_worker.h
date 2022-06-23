/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include <functional>

#include <unistd.h>

namespace agents {

    /**
     * Base interface for agent process workers
     */

    class i_agent_worker_t {
    public:
        /** Enumerates the possible states the agent can be in */
        enum class state_t {
            launched,
            ready,
            shutdown_requested,
            shutdown_received,
            terminated,
        };

        /** Callback used to consume state changes */
        using state_change_observer_t = std::function<void(pid_t, state_t, state_t)>;

        virtual ~i_agent_worker_t() noexcept = default;
        virtual void on_sigchild() = 0;
        virtual void shutdown() = 0;
    };
}
