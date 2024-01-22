/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#include "agents/agent_workers_process_holder.h"

#include "agents/agent_workers_process.h"
#include "agents/spawn_agent.h"

#include <memory>

namespace agents {
    i_agent_worker_manager_callbacks_t::~i_agent_worker_manager_callbacks_t() noexcept = default;

    agent_worker_manager_holder_t::agent_worker_manager_holder_t(i_agent_worker_manager_callbacks_t & callbacks,
                                                                 agents::i_agent_spawner_t & hi_priv_spawner,
                                                                 agents::i_agent_spawner_t & lo_priv_spawner)
        : worker(std::make_unique<agent_workers_process_default_t>(callbacks, hi_priv_spawner, lo_priv_spawner))
    {
    }
}
