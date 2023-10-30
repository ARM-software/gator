/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#pragma once

namespace agents {
    /**
     * The shell-side agent worker process manager.
     *
     * This class maintains the set of all active agent process connections.
     * It is responsible for spawning the agent processes, constructing the local wrapper objects
     * for the workers that communicate with those processes, responding for signals including observing
     * SIGCHLD events and reaping the agent processes when they terminate.
     *
     * @tparam Parent The type of the parent class that owns this object and that receives callbacks notifying
     * of certain events.
     */
    template<typename Parent>
    class agent_workers_process_manager_t;

    /**
     * The io_context, worker threads and signal_set for the agent worker processes manager. Decoupled to allow the worker process manager to be unit tested.
     *
     * @tparam WorkerManager The agent process manager class (usually agent_workers_process_manager_t)
     */
    template<typename WorkerManager>
    class agent_workers_process_context_t;

    /** Convenience alias for the context and manager */
    template<typename Parent>
    using agent_workers_process_t = agent_workers_process_context_t<agent_workers_process_manager_t<Parent>>;
}
