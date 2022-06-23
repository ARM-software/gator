/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once
#include "agents/agent_worker.h"
#include "agents/ext_source/ext_source_agent_worker.h"
#include "async/completion_handler.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/continuation.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "lib/String.h"
#include "lib/Syscall.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <tuple>

#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/system/error_code.hpp>

#include <sys/prctl.h>

namespace agents {
    /**
     * The io_context, worker threads and signal_set for the agent worker processes manager. Decoupled to allow the worker process manager to be unit tested.
     *
     * @tparam Parent The parent class, that owns the agent processes (usually Child)
     * @tparam WorkerManager The agent process manager class (usually agent_workers_process_manager_t)
     */
    template<typename Parent, typename WorkerManager>
    class agent_workers_process_context_t {
    public:
        static constexpr std::size_t n_threads = 2;

        agent_workers_process_context_t(Parent & parent, i_agent_spawner_t & spawner)
            : worker_manager(io_context, parent, spawner)
        {
            signal_set.add(SIGHUP);
            signal_set.add(SIGINT);
            signal_set.add(SIGTERM);
            signal_set.add(SIGABRT);
            signal_set.add(SIGCHLD);
        }

        /** Start the worker. Agents must be spawned separately once the worker has started */
        bool start()
        {
            LOG_DEBUG("Started worker process loop");

            // start the signal handler
            spawn_signal_handler();

            // start the io context on the thread pool (as the caller expects this function to return immediately)
            for (std::size_t i = 0; i < n_threads; ++i) {
                boost::asio::post(threads, [this, i]() { run_io_context_loop(i); });
            }

            return true;
        }

        /** Join the worker. This function will return once all the agents are terminated and any worker threads have exited. */
        void join()
        {
            using namespace async::continuations;

            LOG_DEBUG("Join requested");

            // terminate the worker_manager
            worker_manager.async_shutdown();

            // wait for the thread pool to finish
            threads.join();

            LOG_DEBUG("Join completed");
        }

        /**
         * Add the 'external source' agent worker
         *
         * @param external_souce A reference to the ExternalSource class which receives data from the agent process
         * @param token Some completion token, called asynchronously once the agent is ready
         * @return depends on completion token type
         */
        template<typename ExternalSource, typename CompletionToken>
        auto async_add_external_source(ExternalSource & external_souce, CompletionToken && token)
        {
            return worker_manager.template async_add_agent<ext_source_agent_worker_t<ExternalSource>>(
                std::forward<CompletionToken>(token),
                std::ref(external_souce));
        }

    private:
        boost::asio::io_context io_context {};
        boost::asio::signal_set signal_set {io_context};
        boost::asio::thread_pool threads {n_threads};
        WorkerManager worker_manager;

        /** Handle some signals */
        void spawn_signal_handler()
        {
            using namespace async::continuations;

            repeatedly([this]() { return !worker_manager.is_terminated(); }, //
                       [this]() {
                           return signal_set.async_wait(use_continuation) //
                                | map_error()                             //
                                | then([this](int signo) { worker_manager.on_signal(signo); });
                       }) //
                | DETACH_LOG_ERROR("Signal handler loop");
        }

        /** The worker thread body */
        void run_io_context_loop(std::size_t thread_no) noexcept
        {
            constexpr std::size_t comm_len = 16;

            LOG_DEBUG("Launched worker thread %zu", thread_no);

            lib::printf_str_t<comm_len> comm_str {"gatord-iocx-%zu", thread_no};

            prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(comm_str.c_str()), 0, 0, 0);

            // spin the io_context
            io_context.run();
        }
    };

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
    class agent_workers_process_manager_t {
    public:
        /**
         * Constructor
         *
         * @param io_context A reference to the io_context to use
         * @param parent A reference to the owning object that is to receive certain notifications
         * @param spawner A reference to the agent spawner object, that will launch gatord binary as agent processes
         */
        agent_workers_process_manager_t(boost::asio::io_context & io_context,
                                        Parent & parent,
                                        i_agent_spawner_t & spawner)
            : parent(parent), spawner(spawner), io_context(io_context)
        {
        }

        /** @return True if the worker manager is terminated */
        [[nodiscard]] bool is_terminated() const noexcept { return terminated; }

        /** Called to handle some signal */
        void on_signal(int signo)
        {
            if ((signo == SIGHUP) || (signo == SIGINT) || (signo == SIGTERM) || (signo == SIGABRT)) {
                LOG_DEBUG("Received signal %d", signo);
                parent.on_terminal_signal(signo);
            }
            else if (signo == SIGCHLD) {
                LOG_DEBUG("Received sigchld");
                do_waitpid_children();
            }
            else {
                LOG_DEBUG("Unexpected signal # %d", signo);
            }
        }

        /** Terminate the worker. This function will return once all the agents are terminated and any worker threads have exited. */
        void async_shutdown()
        {
            using namespace async::continuations;

            // spawn an async operation on the strand that performs the shutdown
            start_on(strand) //
                | then([this]() {
                      if (agent_workers.empty()) {
                          terminate();
                      }
                      else {
                          LOG_DEBUG("Requesting all agents to shut down");
                          for (auto & agent : agent_workers) {
                              agent.second->shutdown();
                          }
                      }
                  }) //
                | DETACH_LOG_ERROR("Join operation");
        }

        /**
         * Construct a new worker object for some newly spawned agent and add it to the set of workers
         *
         * @tparam WorkerType The type for the worker class that owns the shell-side of the IPC relationship
         * @param token The async completion token
         * @param args Any additional arguments that may be passed to the constructor of the worker type (any references must be wrapped in a std::reference_wrapper or similar)
         * @return Depends on the completion token type
         */
        template<typename WorkerType, typename CompletionToken, typename... Args>
        auto async_add_agent(CompletionToken && token, Args &&... args)
        {
            return async::continuations::async_initiate_explicit<void(bool)>(
                [this](auto && receiver, auto && exceptionally, auto &&... args) mutable {
                    this->do_async_add_agent<WorkerType>(std::forward<decltype(receiver)>(receiver),
                                                         std::forward<decltype(exceptionally)>(exceptionally),
                                                         std::forward<decltype(args)>(args)...);
                },
                token,
                std::forward<Args>(args)...);
        }

    private:
        Parent & parent;
        i_agent_spawner_t & spawner;
        boost::asio::io_context & io_context;
        boost::asio::io_context::strand strand {io_context};
        std::map<pid_t, std::shared_ptr<i_agent_worker_t>> agent_workers {};

        bool created_any = false;
        bool terminated = false;

        /** terminate the worker loop */
        void terminate()
        {
            LOG_DEBUG("All agents exited, terminating");
            terminated = true;
            io_context.stop();
            parent.on_agent_thread_terminated();
        }

        /** Check if should stop */
        void on_strand_check_terminated()
        {
            if (created_any && agent_workers.empty()) {
                terminate();
            }
        }

        /** Process the async request to start the external source worker */
        template<typename WorkerType, typename Receiver, typename Exceptionally, typename... Args>
        void do_async_add_agent(Receiver receiver, Exceptionally && exceptionally, Args &&... args)
        {
            using namespace async::continuations;

            LOG_DEBUG("Creating ext_source agent process");

            submit(start_on(strand) //
                       | then([this,
                               r = std::forward<Receiver>(receiver),
                               args_tuple =
                                   std::make_tuple<std::decay_t<Args>...>(std::forward<Args>(args)...)]() mutable {
                             std::apply(
                                 [this, r = std::move(r)](auto &&... args) mutable {
                                     if (!on_strand_create_worker<WorkerType>(std::move(r),
                                                                              spawner,
                                                                              std::forward<decltype(args)>(args)...)) {
                                         LOG_ERROR("Could not start external source worker");
                                     }
                                 },
                                 std::move(args_tuple));
                         }),
                   std::forward<Exceptionally>(exceptionally));
        }

        /** Create one worker and add it to the map */
        template<typename T, typename Handler, typename... Args>
        bool on_strand_create_worker(Handler handler, i_agent_spawner_t & spawner, Args &&... args)
        {
            // do nothing if already terminated
            if (terminated) {
                boost::asio::post(io_context, [handler = std::move(handler)]() { handler(false); });
                return false;
            }

            // start the process, returning the wrapper instance
            auto worker =
                spawn_agent_worker<T>(io_context, spawner, make_state_observer(handler), std::forward<Args>(args)...);

            // spawn failed, just let the handler know directly
            if (!worker.second) {
                boost::asio::post(io_context, [handler = std::move(handler)]() { handler(false); });
                return false;
            }

            // great, the handler will be called once the agent is ready
            created_any = true;
            agent_workers.emplace(worker);
            return true;
        }

        /** Construct the state observer object for some agent process. This function will
         * process state changes, and update this class's state as appropriate. It will
         * also notify the agent-process-started handler at the correct time. */
        template<typename Handler>
        auto make_state_observer(Handler handler)
        {
            using namespace async::continuations;

            return [this, handler = std::move(handler), notified_handler = false](auto pid,
                                                                                  auto old_state,
                                                                                  auto new_state) mutable {
                // transition from launched (the initial state) to any other state...
                if ((old_state == i_agent_worker_t::state_t::launched) && !notified_handler) {
                    // handler should only be called once
                    notified_handler = true;
                    // handler receives 'true' for ready and 'false' for all other states as it indicates an error on startup
                    start_with(new_state == i_agent_worker_t::state_t::ready) //
                        | post_on(io_context)                                 //
                        | then(std::move(handler))                            //
                        | DETACH_LOG_ERROR("Notify launch handler operation");
                }

                // transition to terminated
                if (new_state == i_agent_worker_t::state_t::terminated) {
                    start_on(strand) //
                        | then([this, pid]() {
                              LOG_DEBUG("Received agent terminated notification for agent process %d", pid);
                              // remove it
                              agent_workers.erase(pid);
                              // stop if no more agents
                              on_strand_check_terminated();
                          }) //
                        | DETACH_LOG_ERROR("Handle agent terminated notification operation");
                }
            };
        }

        /** Handle the sigchld event */
        void do_waitpid_children()
        {
            using namespace async::continuations;

            // iterate each child agent and check if it terminated.
            // if so, notify its worker and remove it from the map.
            //
            // We don't use waitpid(0 or -1, ...) since there are other waitpid calls that block on a single pid and we dont
            // want to swallow the process event from them
            start_on<on_executor_mode_t::dispatch>(strand) //
                | then([this]() {
                      // check all the child processes
                      for (auto it = agent_workers.begin(); it != agent_workers.end();) {
                          if (do_waitpid_for(it->first, it->second)) {
                              it = agent_workers.erase(it);
                          }
                          else {
                              ++it;
                          }
                      }

                      // stop if no more items
                      on_strand_check_terminated();
                  })
                | DETACH_LOG_ERROR("SIGCHLD handler");
        }

        /** Check the exit status for some worker process */
        bool do_waitpid_for(pid_t agent_pid, std::shared_ptr<i_agent_worker_t> worker)
        {
            int wstatus = 0;
            pid_t result = lib::waitpid(agent_pid, &wstatus, WNOHANG);
            int error = errno;

            LOG_TRACE("Got waitpid(result=%d, wstatus=%d, pid=%d, worker=%p)",
                      result,
                      wstatus,
                      agent_pid,
                      worker.get());

            // call waitpid to reap the child pid, but nothing to do
            if (worker == nullptr) {
                LOG_DEBUG("Unexpected state, received SIGCHLD for pid=%d, but no worker found", agent_pid);
                return true;
            }

            auto const process_exited = ((result == agent_pid) && (WIFEXITED(wstatus) || WIFSIGNALED(wstatus)));
            auto const no_such_child = ((result == pid_t(-1)) && (error == ECHILD));

            // the process terminated, or no such child exists, notify the worker class to update its state machine
            if (process_exited || no_such_child) {
                // notify of the signal
                LOG_DEBUG("Notifying worker that agent process %d (%p) terminated.", agent_pid, worker.get());
                worker->on_sigchild();

                // dont erase it yet, wait for the state machine to update
                return false;
            }

            // some other error occured, log it
            if (result == pid_t(-1)) {
                // NOLINTNEXTLINE(concurrency-mt-unsafe)
                LOG_DEBUG("waitpid received error %d %s", error, strerror(error));
            }

            return false;
        }
    };

    /** Convenience alias for the context and manager */
    template<typename Parent>
    using agent_workers_process_t = agent_workers_process_context_t<Parent, agent_workers_process_manager_t<Parent>>;
}
