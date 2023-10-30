/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */
#pragma once

#include "Logging.h"
#include "agents/agent_worker.h"
#include "async/continuations/async_initiate.h"
#include "ipc/raw_ipc_channel_sink.h"
#include "ipc/raw_ipc_channel_source.h"
#include "lib/error_code_or.hpp"
#include "lib/forked_process.h"
#include "logging/agent_log.h"

#include <optional>
#include <string_view>

#include <boost/asio/io_context.hpp>

namespace agents {

    /** ID string used to identify the armnn agent */
    constexpr std::string_view agent_id_armnn {"agent-armnn"};

    /** ID string used to identify the external annotation agent */
    constexpr std::string_view agent_id_ext_source {"agent-external"};

    /** ID string used to identify the external annotation agent */
    constexpr std::string_view agent_id_perf {"agent-perf"};

    /** ID string used to identify the perfetto agent */
    constexpr std::string_view agent_id_perfetto {"agent-perfetto"};

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
        virtual lib::error_code_or_t<lib::forked_process_t> spawn_agent_process(char const * agent_name) = 0;
    };

    /**
     * Default, simple implementation of i_agent_spawner_t that just forks/exec the current process binary
     */
    class simple_agent_spawner_t final : public i_agent_spawner_t {
    public:
        lib::error_code_or_t<lib::forked_process_t> spawn_agent_process(char const * agent_name) override;
    };

    /**
     * Android implementation of i_agent_spawner_t that runs the agent using `run-as` within some package
     */
    class android_pkg_agent_spawner_t final : public i_agent_spawner_t {
    public:
        explicit android_pkg_agent_spawner_t(std::string package_name) : package_name(std::move(package_name)) {}
        android_pkg_agent_spawner_t(android_pkg_agent_spawner_t const &) = delete;
        android_pkg_agent_spawner_t & operator=(android_pkg_agent_spawner_t const &) = delete;
        android_pkg_agent_spawner_t(android_pkg_agent_spawner_t &&) noexcept = default;
        android_pkg_agent_spawner_t & operator=(android_pkg_agent_spawner_t &&) noexcept = default;
        ~android_pkg_agent_spawner_t() noexcept override;

        lib::error_code_or_t<lib::forked_process_t> spawn_agent_process(char const * agent_name) override;

    private:
        std::string package_name;
        std::optional<std::string> remote_exe_path {};
    };

    /**
     * The spawned agent resutl object
     */
    struct spawn_agent_result_t {
        /** The IPC source, for reading messages from the agent */
        std::shared_ptr<ipc::raw_ipc_channel_source_t> ipc_source;
        /** The IPC sink, for sending message to the agent */
        std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink;
        /** The agent log reader and consumer */
        std::shared_ptr<logging::agent_log_reader_t> agent_log_reader;
        /** The forked process object */
        lib::forked_process_t forked_process;
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
        /** The forked process object */
        lib::forked_process_t forked_process;
    };

    /**
     * Spawn an agent process
     *
     * @param io_context The io_context for async operations
     * @param spawner The process spawner
     * @param agent_name The agent ID
     * @param log_consumer An agent log consumer function
     * @return The spawned process properties (or error if failed)
     */
    lib::error_code_or_t<spawn_agent_result_t> spawn_agent(boost::asio::io_context & io_context,
                                                           i_agent_spawner_t & spawner,
                                                           char const * agent_name,
                                                           logging::agent_log_reader_t::consumer_fn_t log_consumer);

    /**
     * Spawn an agent process using the default log consumer
     *
     * @param io_context The io_context for async operations
     * @param spawner The process spawner
     * @param agent_name The agent ID
     * @return The spawned process properties (or error if failed)
     */
    lib::error_code_or_t<spawn_agent_result_t> spawn_agent(boost::asio::io_context & io_context,
                                                           i_agent_spawner_t & spawner,
                                                           char const * agent_name);

    /**
     * Spawn an agent process and construct the associated worker class that owns the IPC objects and interacts with the agent
     *
     * @tparam T The worker wrapper class type
     *
     * @param io_context The io_context for async operations
     * @param spawner The process spawner
     * @param observer The state change observer callback
     *
     * Async operation produces a pair containing the process pid and a shared_ptr to T (or `{0, nullptr}` if an error occured)
     */
    template<typename T, typename CompletionToken, typename... Args>
    inline auto async_spawn_agent_worker(boost::asio::io_context & io_context,
                                         i_agent_spawner_t & spawner,
                                         i_agent_worker_t::state_change_observer_t && observer,
                                         CompletionToken && token,
                                         Args &&... args)
    {
        using namespace async::continuations;

        return async_initiate_cont(
            [&io_context, &spawner, observer = std::move(observer)](auto &&... args) mutable {
                return start_with(std::forward<decltype(args)>(args)...) //
                     | then([&io_context, &spawner, observer = std::move(observer)](
                                auto &&... args) mutable -> std::pair<pid_t, std::shared_ptr<T>> {
                           // spawn the process
                           auto spawn_result = spawn_agent(io_context, spawner, T::get_agent_process_id());
                           if (auto const * error = lib::get_error(spawn_result)) {
                               return {0, nullptr};
                           }

                           // get the value from the spawn result
                           auto spawn_properties = lib::get_value(std::move(spawn_result));
                           auto const pid = spawn_properties.forked_process.get_pid();

                           // construct the worker class
                           auto ptr = std::make_shared<T>(io_context,
                                                          agent_process_t {
                                                              spawn_properties.ipc_source,
                                                              spawn_properties.ipc_sink,
                                                              spawn_properties.agent_log_reader,
                                                              std::move(spawn_properties.forked_process),
                                                          },
                                                          std::move(observer),
                                                          std::forward<decltype(args)>(args)...);

                           // start it - this should exec the agent, returning the result of the exec command
                           if (!ptr->start()) {
                               LOG_ERROR("Agent process created, but exec failed");
                           }

                           return {pid, std::move(ptr)};
                       });
            },
            std::forward<CompletionToken>(token),
            std::forward<Args>(args)...);
    }
}
