/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */
#include "agents/spawn_agent.h"

#include "android/Spawn.h"
#include "lib/Assert.h"
#include "lib/AutoClosingFd.h"
#include "lib/FsEntry.h"
#include "lib/Process.h"
#include "lib/error_code_or.hpp"
#include "lib/forked_process.h"

#include <boost/system/errc.hpp>

namespace agents {
    /** Simple agent spawner */
    lib::error_code_or_t<lib::forked_process_t> simple_agent_spawner_t::spawn_agent_process(char const * agent_name)
    {
        runtime_assert(agent_name != nullptr, "agent_name is required");

        auto proc_self_exe = lib::FsEntry::create("/proc/self/exe");
        auto gatord_exe = proc_self_exe.realpath();

        if (!gatord_exe) {
            LOG_ERROR("Could not resolve /proc/self/exe to gatord's real path. Did it get deleted?");
            return boost::system::errc::make_error_code(boost::system::errc::no_such_file_or_directory);
        }

        auto stdio_fds = lib::stdio_fds_t::create_pipes();
        if (auto const * error = lib::get_error(stdio_fds)) {
            return *error;
        }

        std::vector<std::string> arguments {agent_name};

        if (::logging::is_log_enable_trace()) {
            arguments.emplace_back("--trace");
        }

        return lib::forked_process_t::fork_process(true,
                                                   gatord_exe->path(),
                                                   arguments,
                                                   {},
                                                   {},
                                                   lib::get_value(std::move(stdio_fds)));
    }

    android_pkg_agent_spawner_t::~android_pkg_agent_spawner_t() noexcept
    {
        if (remote_exe_path) {
            gator::process::system("run-as '" + package_name + "' rm -f '" + *remote_exe_path + "'");
        }
    }

    /** Android agent spawner */
    lib::error_code_or_t<lib::forked_process_t> android_pkg_agent_spawner_t::spawn_agent_process(
        char const * agent_name)
    {
#ifdef APP_GATOR_GDB_SERVER
        constexpr std::size_t extra_args = 2;
#else
        constexpr std::size_t extra_args = 0;
#endif

        runtime_assert(agent_name != nullptr, "agent_name is required");

        if (!remote_exe_path) {
            remote_exe_path = gator::android::deploy_to_package(package_name);
            if (!remote_exe_path) {
                return boost::system::errc::make_error_code(boost::system::errc::permission_denied);
            }
        }

        auto stdio_fds = lib::stdio_fds_t::create_pipes();
        if (auto const * error = lib::get_error(stdio_fds)) {
            return *error;
        }

        std::array<std::string, 3 + extra_args> arguments {{
            package_name,
#ifdef APP_GATOR_GDB_SERVER
            "./gdbserver",
            ":5001",
#endif
            *remote_exe_path,
            agent_name,
        }};

        return lib::forked_process_t::fork_process(true,
                                                   "run-as",
                                                   arguments,
                                                   {},
                                                   {},
                                                   lib::get_value(std::move(stdio_fds)));
    }

    /** Spawn the agent */
    lib::error_code_or_t<spawn_agent_result_t> spawn_agent(boost::asio::io_context & io_context,
                                                           i_agent_spawner_t & spawner,
                                                           char const * agent_name,
                                                           logging::agent_log_reader_t::consumer_fn_t log_consumer)
    {
        auto result = spawner.spawn_agent_process(agent_name);
        if (auto const * error = lib::get_error(result)) {
            return *error;
        }

        auto process = lib::get_value(std::move(result));
        auto ipc_source = ipc::raw_ipc_channel_source_t::create(io_context, std::move(process.get_stdout_read()));
        auto ipc_sink = ipc::raw_ipc_channel_sink_t::create(io_context, std::move(process.get_stdin_write()));
        auto log_reader = logging::agent_log_reader_t::create(io_context,
                                                              std::move(process.get_stderr_read()),
                                                              std::move(log_consumer));

        return spawn_agent_result_t {
            std::move(ipc_source),
            std::move(ipc_sink),
            std::move(log_reader),
            std::move(process),
        };
    }

    /** Spawn the agent with the default logger */
    lib::error_code_or_t<spawn_agent_result_t> spawn_agent(boost::asio::io_context & io_context,
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
