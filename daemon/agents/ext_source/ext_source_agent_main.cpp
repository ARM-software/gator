/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */
#include "agents/ext_source/ext_source_agent_main.h"

#include "Logging.h"
#include "agents/ext_source/ext_source_agent.h"
#include "ipc/raw_ipc_channel_sink.h"
#include "ipc/raw_ipc_channel_source.h"
#include "lib/AutoClosingFd.h"
#include "logging/agent_log.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <sys/prctl.h>
#include <unistd.h>

namespace agents {
    namespace {
        lib::AutoClosingFd dup_and_close(int fd)
        {
            lib::AutoClosingFd dup_fd {fcntl(fd, F_DUPFD_CLOEXEC)};

            if (!dup_fd) {
                // NOLINTNEXTLINE(concurrency-mt-unsafe)
                LOG_DEBUG("fcntl failed with error %d (%s)", errno, strerror(errno));

                // not ideal, but just use the FD directly
                return lib::AutoClosingFd {fd};
            }

            // now close it
            close(fd);
            return dup_fd;
        }

        void do_wait_signal(boost::asio::signal_set & signals, std::shared_ptr<ext_source_agent_t> agent)
        {
            signals.async_wait([agent = std::move(agent), &signals](auto const & ec, auto signo) mutable {
                if (ec) {
                    LOG_DEBUG("Signal handler received error %s", ec.message().c_str());
                    return;
                }
                //NOLINTNEXTLINE(concurrency-mt-unsafe)
                LOG_DEBUG("Received signal %d %s", signo, strsignal(signo));
                if ((signo == SIGHUP) || (signo == SIGTERM) || (signo == SIGINT)) {
                    agent->shutdown();
                }
                else {
                    do_wait_signal(signals, std::move(agent));
                }
            });
        }
    }

    int ext_agent_main(char const * /*argv0*/, lib::Span<char const * const> args)
    {
        // set process name
        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-agent-xs"), 0, 0, 0);

        // Set up global thread-safe logging
        auto agent_logging =
            std::make_shared<logging::agent_log_sink_t>(STDERR_FILENO, logging::agent_log_sink_t::get_log_file_fd());

        logging::set_log_sink(agent_logging);
        logging::set_log_enable_trace(args);

        try {
            LOG_DEBUG("Started ext_agent_main");

            // disable buffering on in/out/err
            ::setvbuf(stdin, nullptr, _IONBF, 0);
            ::setvbuf(stdout, nullptr, _IONBF, 0);
            ::setvbuf(stderr, nullptr, _IONBF, 0);

            // get sighup if parent exits
            ::prctl(PR_SET_PDEATHSIG, SIGHUP);

            // duplicate stdin/stdout, then close them so that some spurious read/write doesn't corrupt the IPC channel
            auto ipc_in = dup_and_close(STDIN_FILENO);
            auto ipc_out = dup_and_close(STDOUT_FILENO);

            // setup asio context
            boost::asio::io_context io_context {};

            // handle the usual signals (and SIGHUP) so we can shutdown properly
            boost::asio::signal_set signals {io_context, SIGHUP, SIGTERM, SIGINT};

            // create our IPC channels
            auto ipc_sink = ipc::raw_ipc_channel_sink_t::create(io_context, std::move(ipc_out));
            auto ipc_source = ipc::raw_ipc_channel_source_t::create(io_context, std::move(ipc_in));

            // create our agent
            auto agent = ext_source_agent_t::create(io_context, ipc_sink, ipc_source);
            agent->add_all_defaults();

            // handle signals
            do_wait_signal(signals, agent);
            // and shutdown
            agent->async_wait_shutdown([&io_context]() {
                // fully shut down
                LOG_DEBUG("Agent is shutdown. Stopping io_context.");
                io_context.stop();
            });

            // start the agent
            agent->start();

            // run the main work loop
            io_context.run();
        }
        catch (std::exception const & ex) {
            LOG_FATAL("Unexpected exception received: what=%s", ex.what());
            return EXIT_FAILURE;
        }
        catch (...) {
            LOG_FATAL("Unexpected exception received.");
            return EXIT_FAILURE;
        }

        LOG_DEBUG("Terminating ext_source agent successfully.");

        return EXIT_SUCCESS;
    }
}
