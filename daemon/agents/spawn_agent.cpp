/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */
#include "agents/spawn_agent.h"

#include "lib/Assert.h"
#include "lib/AutoClosingFd.h"
#include "lib/FsEntry.h"

namespace agents {
    /** Simple agent spawner */
    std::optional<lib::PopenResult> simple_agent_spawner_t::spawn_agent_process(char const * agent_name)
    {
        runtime_assert(agent_name != nullptr, "agent_name is required");

        auto proc_self_exe = lib::FsEntry::create("/proc/self/exe");
        auto gatord_exe = proc_self_exe.realpath();

        if (!gatord_exe) {
            LOG_ERROR("Could not resolve /proc/self/exe to gatord's real path. Did it get deleted?");
            return {};
        }

        return lib::popen(gatord_exe->path().c_str(), agent_name);
    }

    /** Spawn the agent */
    std::optional<agent_process_t> spawn_agent(boost::asio::io_context & io_context,
                                               i_agent_spawner_t & spawner,
                                               char const * agent_name,
                                               logging::agent_log_reader_t::consumer_fn_t log_consumer)
    {
        auto process = spawner.spawn_agent_process(agent_name);
        if (!process) {
            return {};
        }

        return agent_process_t {
            ipc::raw_ipc_channel_source_t::create(io_context, lib::AutoClosingFd {process->out}),
            ipc::raw_ipc_channel_sink_t::create(io_context, lib::AutoClosingFd {process->in}),
            logging::agent_log_reader_t::create(io_context, lib::AutoClosingFd {process->err}, std::move(log_consumer)),
            process->pid,
        };
    }

    /** Spawn the agent with the default logger */
    std::optional<agent_process_t> spawn_agent(boost::asio::io_context & io_context,
                                               i_agent_spawner_t & spawner,
                                               char const * agent_name)
    {
        return spawn_agent(io_context,
                           spawner,
                           agent_name,
                           [](auto tid, auto level, auto timestamp, auto location, auto message) {
                               logging::log_item(tid, level, timestamp, location, message);
                           });
    }
}
