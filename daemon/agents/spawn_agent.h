/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */
#pragma once

#include "Logging.h"
#include "agents/agent_worker.h"
#include "ipc/raw_ipc_channel_sink.h"
#include "ipc/raw_ipc_channel_source.h"
#include "lib/Popen.h"
#include "logging/agent_log.h"

#include <optional>
#include <string_view>

#include <boost/asio/io_context.hpp>

namespace agents {

    /** ID string used to identify the external annotation agent */
    constexpr std::string_view agent_id_ext_source {"agent-external"};

    /**
     * An interface for some class that will spawn a gatord agent process
     */
    class i_agent_spawner_t {
    public:
        virtual ~i_agent_spawner_t() noexcept = default;
        /**
         * Spawn the agent process with the specified ID
         *
         * @param agent_name The agent ID string
         * @return The process popen result
         */
        virtual std::optional<lib::PopenResult> spawn_agent_process(char const * agent_name) = 0;
    };

    /**
     * Default, simple implementation of i_agent_spawner_t that just forks/exec the current process binary
     */
    class simple_agent_spawner_t : public i_agent_spawner_t {
    public:
        std::optional<lib::PopenResult> spawn_agent_process(char const * agent_name) override;
    };

    /**
     * The common agent process properties for some launched agent process
     */
    struct agent_process_t {
        /** The IPC source, for reading messages from the agent */
        std::shared_ptr<ipc::raw_ipc_channel_source_t> ipc_source;
        /** The IPC sink, for sending message to the agent */
        std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink;
        /** The agent log reader and consumer */
        std::shared_ptr<logging::agent_log_reader_t> agent_log_reader;
        /** The agent process pid */
        pid_t pid;
    };

    /**
     * Spawn an agent process
     *
     * @param io_context The io_context for async operations
     * @param spawner The process spawner
     * @param agent_name The agent ID
     * @param log_consumer An agent log consumer function
     * @return The spawned process properties (or empty if popen failed)
     */
    std::optional<agent_process_t> spawn_agent(boost::asio::io_context & io_context,
                                               i_agent_spawner_t & spawner,
                                               char const * agent_name,
                                               logging::agent_log_reader_t::consumer_fn_t log_consumer);

    /**
     * Spawn an agent process using the default log consumer
     *
     * @param io_context The io_context for async operations
     * @param spawner The process spawner
     * @param agent_name The agent ID
     * @return The spawned process properties (or empty if popen failed)
     */
    std::optional<agent_process_t> spawn_agent(boost::asio::io_context & io_context,
                                               i_agent_spawner_t & spawner,
                                               char const * agent_name);

    /**
     * Spawn an agent process and construct the associated worker class that owns the IPC objects and interacts with the agent
     *
     * @tparam T The worker wrapper class type
     * @param io_context The io_context for async operations
     * @param spawner The process spawner
     * @return A pair containing the process pid and a shared_ptr to T (or `{0, nullptr}` if an error occured)
     */
    template<typename T, typename... Args>
    static std::pair<pid_t, std::shared_ptr<T>> spawn_agent_worker(
        boost::asio::io_context & io_context,
        i_agent_spawner_t & spawner,
        i_agent_worker_t::state_change_observer_t && observer,
        Args &&... args)
    {
        // spawn the process
        auto agent = spawn_agent(io_context, spawner, T::get_agent_process_id());
        if (!agent) {
            return {0, nullptr};
        }

        // construct the worker class
        auto ptr = std::make_shared<T>(io_context, *agent, std::move(observer), std::forward<Args>(args)...);

        // start it (after construction since it is assumed to be enable_shared_from_this)
        ptr->start();

        return {agent->pid, std::move(ptr)};
    }
}
