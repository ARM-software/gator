/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#pragma once

#include "agents/agent_workers_process_fwd.h"
#include "agents/spawn_agent.h"

#include <memory>

namespace agents {
    /** Notifications interface for the agent workers process */
    class i_agent_worker_manager_callbacks_t {
    public:
        virtual ~i_agent_worker_manager_callbacks_t() noexcept;

        virtual void on_agent_thread_terminated() = 0;
        virtual void on_terminal_signal(int sig_no) = 0;
    };

    using agent_workers_process_default_t = agent_workers_process_t<i_agent_worker_manager_callbacks_t>;

    /** Container for the worker object */
    class agent_worker_manager_holder_t {
    public:
        agent_worker_manager_holder_t(i_agent_worker_manager_callbacks_t & callbacks,
                                      agents::i_agent_spawner_t & hi_priv_spawner,
                                      agents::i_agent_spawner_t & lo_priv_spawner);

        agent_workers_process_default_t * operator->() { return worker.get(); }

        agent_workers_process_default_t const * operator->() const { return worker.get(); }

        agent_workers_process_default_t & operator*() { return *worker; }

        agent_workers_process_default_t const & operator*() const { return *worker; }

    private:
        std::unique_ptr<agent_workers_process_default_t> worker;
    };
}
