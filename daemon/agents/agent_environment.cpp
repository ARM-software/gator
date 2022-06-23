/* Copyright (C) 2022 by Arm Limited. All rights reserved. */
#include "agents/agent_environment.h"

#include "Logging.h"
#include "agents/ext_source/ext_source_agent.h"
#include "ipc/raw_ipc_channel_sink.h"
#include "ipc/raw_ipc_channel_source.h"
#include "lib/AutoClosingFd.h"
#include "lib/String.h"
#include "logging/agent_log.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/thread_pool.hpp>

#include <sys/prctl.h>
#include <unistd.h>

namespace agents {
    namespace {
        constexpr std::size_t n_threads = 2;

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

        void do_wait_signal(boost::asio::signal_set & signals,
                            std::shared_ptr<agent_environment_base_t> env,
                            async::proc::process_monitor_t & process_monitor)
        {
            signals.async_wait([env = std::move(env), &signals, &process_monitor](auto const & ec, auto signo) mutable {
                if (ec) {
                    LOG_DEBUG("Signal handler received error %s", ec.message().c_str());
                    return;
                }
                //NOLINTNEXTLINE(concurrency-mt-unsafe)
                LOG_DEBUG("Received signal %d %s", signo, strsignal(signo));
                if ((signo == SIGHUP) || (signo == SIGTERM) || (signo == SIGINT)) {
                    env->shutdown();
                }
                else if (signo == SIGCHLD) {
                    process_monitor.on_sigchild();
                }
                else {
                    do_wait_signal(signals, std::move(env), process_monitor);
                }
            });
        }
    }

    int start_agent(lib::Span<char const * const> args, const environment_factory_t & factory)
    {
        // set process name
        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-agent-bootstrap"), 0, 0, 0);

        // Set up global thread-safe logging
        auto agent_logging =
            std::make_shared<logging::agent_log_sink_t>(STDERR_FILENO, logging::agent_log_sink_t::get_log_file_fd());

        logging::set_log_sink(agent_logging);
        logging::set_log_enable_trace(args);

        try {
            LOG_DEBUG("Bootstrapping agent process.");

            // disable buffering on in/out/err
            ::setvbuf(stdin, nullptr, _IONBF, 0);
            ::setvbuf(stdout, nullptr, _IONBF, 0);
            ::setvbuf(stderr, nullptr, _IONBF, 0);

            // get sighup if parent exits
            ::prctl(PR_SET_PDEATHSIG, SIGKILL);

            // duplicate stdin/stdout, then close them so that some spurious read/write doesn't corrupt the IPC channel
            auto ipc_in = dup_and_close(STDIN_FILENO);
            auto ipc_out = dup_and_close(STDOUT_FILENO);

            // setup asio context
            boost::asio::io_context io_context {};

            // process monitor
            async::proc::process_monitor_t process_monitor {io_context};

            // handle the usual signals (and SIGHUP) so we can shutdown properly
            boost::asio::signal_set signals {io_context};
            signals.add(SIGCHLD);
            signals.add(SIGHUP);
            signals.add(SIGTERM);
            signals.add(SIGINT);

            // create our IPC channels
            auto ipc_sink = ipc::raw_ipc_channel_sink_t::create(io_context, std::move(ipc_out));
            auto ipc_source = ipc::raw_ipc_channel_source_t::create(io_context, std::move(ipc_in));

            // create our agent
            auto env = factory(args, io_context, process_monitor, ipc_sink, ipc_source);
            // set process name
            prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(env->name()), 0, 0, 0);
            LOG_DEBUG("Starting agent [%s]", env->name());

            // handle signals
            do_wait_signal(signals, env, process_monitor);

            async_await_agent_shutdown(env, [&io_context]() {
                // fully shut down
                LOG_DEBUG("Agent is shutdown. Stopping io_context.");
                io_context.stop();
            });

            // start the agent
            env->start();

            // provide extra threads by way of pool
            boost::asio::thread_pool threads {n_threads};

            // start the io context on the thread pool (as the caller expects this function to return immediately)
            for (std::size_t i = 0; i < n_threads; ++i) {
                boost::asio::post(threads, [thread_no = i, &io_context]() {
                    constexpr std::size_t comm_len = 16;

                    LOG_DEBUG("Launched worker thread %zu", thread_no);

                    lib::printf_str_t<comm_len> comm_str {"gatord-iocx-%zu", thread_no};

                    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(comm_str.c_str()), 0, 0, 0);

                    // spin the io_context
                    io_context.run();
                });
            }

            // run the main work loop
            io_context.run();

            LOG_DEBUG("Terminating [%s] agent successfully.", env->name());
        }
        catch (std::exception const & ex) {
            LOG_FATAL("Unexpected exception received: what=%s", ex.what());
            return EXIT_FAILURE;
        }
        catch (...) {
            LOG_FATAL("Unexpected exception received.");
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }
}
