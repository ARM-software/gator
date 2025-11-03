/* Copyright (C) 2021-2025 by Arm Limited. All rights reserved. */

#pragma once

#include "Config.h"
#include "agents/agent_worker.h"
#include "agents/agent_workers_process_holder.h"
#include "agents/ext_source/ext_source_agent_worker.h"
#include "agents/perf/perf_agent_worker.h"
#include "async/completion_handler.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/continuation.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "async/proc/process_monitor.hpp"
#include "lib/String.h"
#include "lib/Syscall.h"

#if CONFIG_ARMNN_AGENT
#include "agents/armnn/armnn_agent_worker.h"
#endif

#ifdef CONFIG_USE_PERFETTO
#include "agents/perfetto/perfetto_agent_worker.h"
#endif

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
     * An enumeration that shows whether an agent process needs to be executed in a high
     * privilege session (shell), or a low privilege session (Android app user).
     */
    enum class agent_privilege_level_t { high, low };

    /**
     * The io_context, worker threads and signal_set for the agent worker processes manager. Decoupled to allow the worker process manager to be unit tested.
     *
     * @tparam WorkerManager The agent process manager class (usually agent_workers_process_manager_t)
     */
    template<typename WorkerManager>
    class agent_workers_process_context_t {
    public:
        static constexpr std::size_t n_threads = 2;

        template<typename Parent>
        agent_workers_process_context_t(Parent & parent,
                                        i_agent_spawner_t & hi_priv_spawner,
                                        i_agent_spawner_t & lo_priv_spawner)
            : worker_manager(io_context, parent, hi_priv_spawner, lo_priv_spawner)
        {
            signal_set.add(SIGHUP);
            signal_set.add(SIGINT);
            signal_set.add(SIGTERM);
            signal_set.add(SIGABRT);
            signal_set.add(SIGCHLD);
            signal_set.add(SIGALRM);
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
        template<typename ExternalSource, typename ConfigMsg, typename CompletionToken>
        auto async_add_external_source(ExternalSource & external_souce, ConfigMsg && msg, CompletionToken && token)
        {
            return worker_manager.template async_add_agent<ext_source_agent_worker_t<ExternalSource>>(
                process_monitor,
                agent_privilege_level_t::low,
                std::forward<CompletionToken>(token),
                std::ref(external_souce),
                std::forward<ConfigMsg>(msg));
        }

#if CONFIG_ARMNN_AGENT
        /**
         * Add the 'armnn' agent worker
         *
         * @param socket_consumer A reference to an object responsible for forming an armnn::Session from a ISocketIO
         * @param token Some completion token, called asynchronously once the agent is ready
         * @return depends on completion token type
         */
        template<typename CompletionToken>
        auto async_add_armnn_source(armnn::ISocketIOConsumer & socket_consumer, CompletionToken && token)
        {
            return worker_manager.template async_add_agent<armnn_agent_worker_t>(process_monitor,
                                                                                 agent_privilege_level_t::low,
                                                                                 std::forward<CompletionToken>(token),
                                                                                 std::ref(socket_consumer));
        }
#endif

#ifdef CONFIG_USE_PERFETTO

        /**
         * Add the 'perfetto' agent worker
         *
         * @param perfetto_souce A reference to the Perfetto class which receives data from the agent process
         * @param token Some completion token, called asynchronously once the agent is ready
         * @return depends on completion token type
         */
        template<typename Perfetto, typename CompletionToken>
        auto async_add_perfetto_source(Perfetto & perfetto_souce, CompletionToken && token)
        {
            return worker_manager.template async_add_agent<perfetto_agent_worker_t<Perfetto>>(
                process_monitor,
                agent_privilege_level_t::high,
                std::forward<CompletionToken>(token),
                std::ref(perfetto_souce));
        }
#endif

        template<typename EventHandler, typename ConfigMsg, typename CompletionToken>
        auto async_add_perf_source(std::shared_ptr<EventHandler> event_handler,
                                   ConfigMsg && msg,
                                   CompletionToken && token)
        {
            return worker_manager.template async_add_agent<agents::perf::perf_agent_worker_t<EventHandler>>(
                process_monitor,
                agent_privilege_level_t::low,
                std::forward<CompletionToken>(token),
                std::ref(event_handler),
                std::forward<ConfigMsg>(msg));
        }

        /** Broadcast a message to all agents, once they are ready.
         *
         * This will cache messages for not-ready agents, and send them when
         * they become ready.
         * @tparam MessageType IPC message type
         * @tparam CompletionToken Continuation or callback to handle the result
         * @param message Message to send
         * @param token Completion token instance
         * @return Continuation or void if the CompletionToken is a callback
         */
        template<typename MessageType, typename CompletionToken>
        auto async_broadcast_when_ready(MessageType message, CompletionToken && token)
        {
            return worker_manager.async_broadcast_when_ready(message, token);
        }

    private:
        boost::asio::io_context io_context {n_threads};
        async::proc::process_monitor_t process_monitor {io_context};
        boost::asio::signal_set signal_set {io_context};
        boost::asio::thread_pool threads {n_threads};
        WorkerManager worker_manager;

        /** Handle some signals */
        void spawn_signal_handler()
        {
            using namespace async::continuations;

            spawn("Signal handler loop",
                  repeatedly([this]() { return !worker_manager.is_terminated(); }, //
                             [this]() {
                                 return signal_set.async_wait(use_continuation) //
                                      | map_error()                             //
                                      | then([this](int signo) {
                                            if (signo == SIGCHLD) {
                                                process_monitor.on_sigchild();
                                            }
                                            else {
                                                worker_manager.on_signal(signo);
                                            }
                                        });
                             }));
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
         * @param hi_priv_spawner A reference to the agent spawner object that will launch an agent process with a high privilege level.
         * @param lo_priv_spawner A reference to the agent spawner object that will launch an agent process with a low privilege level.
         */
        agent_workers_process_manager_t(boost::asio::io_context & io_context,
                                        Parent & parent,
                                        i_agent_spawner_t & hi_priv_spawner,
                                        i_agent_spawner_t & lo_priv_spawner)
            : parent(parent), hi_priv_spawner(hi_priv_spawner), lo_priv_spawner(lo_priv_spawner), io_context(io_context)
        {
        }

        // reference holder so delete assignment
        agent_workers_process_manager_t & operator=(agent_workers_process_manager_t const &) = delete;
        agent_workers_process_manager_t & operator=(agent_workers_process_manager_t &&) noexcept = delete;

        /** @return True if the worker manager is terminated */
        [[nodiscard]] bool is_terminated() const noexcept { return terminated; }

        /** Called to handle some signal */
        void on_signal(int signo)
        {
            if ((signo == SIGHUP) || (signo == SIGINT) || (signo == SIGTERM) || (signo == SIGABRT)) {
                LOG_DEBUG("Received signal %d", signo);
                parent.on_terminal_signal(signo);
            }
            else if (signo == SIGALRM) {
                if (sigalarm_counter == 0) {
                    LOG_WARNING("alarm received, sender running slowly, possible bottleneck in transmission path");
                }
                else {
                    LOG_DEBUG("alarm received again (#%zu)", sigalarm_counter);
                }
                sigalarm_counter += 1;
            }
            else {
                LOG_WARNING("Unexpected signal # %d", signo);
            }
        }

        /** Terminate the worker. This function will return once all the agents are terminated and any worker threads have exited. */
        void async_shutdown()
        {
            using namespace async::continuations;

            // spawn an async operation on the strand that performs the shutdown
            spawn("Join operation",
                  start_on(strand) //
                      | then([this]() {
                            if (agent_workers.empty()) {
                                terminate();
                            }
                            else {
                                LOG_FINE("Requesting all agents to shut down");
                                for (auto & agent : agent_workers) {
                                    agent.second.worker->shutdown();
                                }
                            }
                        }));
        }

        /**
         * Construct a new worker object for some newly spawned agent and add it to the set of workers
         *
         * @tparam WorkerType The type for the worker class that owns the shell-side of the IPC relationship
         * @param privilege_level The privilege level under which to execute the agent process.
         * @param token The async completion token
         * @param args Any additional arguments that may be passed to the constructor of the worker type (any references must be wrapped in a std::reference_wrapper or similar)
         * @return Depends on the completion token type
         */
        template<typename WorkerType, typename ProcessMonitor, typename CompletionToken, typename... Args>
        auto async_add_agent(ProcessMonitor & process_monitor,
                             agent_privilege_level_t privilege_level,
                             CompletionToken && token,
                             Args &&... args)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [this, &process_monitor, privilege_level](auto &&... args) mutable {
                    LOG_FINE("Creating agent process");

                    return start_with(std::move(args)...) //
                         | post_on(strand)                //
                         | then([this, &process_monitor, privilege_level](
                                    auto &&... args) -> polymorphic_continuation_t<bool> {
                               // do nothing if already terminated
                               if (terminated) {
                                   return start_with(false);
                               }

                               auto & spawner =
                                   privilege_level == agent_privilege_level_t::high ? hi_priv_spawner : lo_priv_spawner;

                               // start the process, returning the wrapper instance
                               return async_spawn_agent_worker<WorkerType>(io_context,
                                                                           spawner,
                                                                           make_state_observer(),
                                                                           use_continuation,
                                                                           std::forward<decltype(args)>(args)...) //
                                    | then([this, &process_monitor](auto worker) -> polymorphic_continuation_t<bool> {
                                          // spawn failed, just let the handler know directly
                                          if (!worker.second) {
                                              return start_with(false);
                                          }

                                          // great, store it
                                          created_any = true;
                                          agent_workers.emplace(worker);

                                          // monitor pid
                                          observe_agent_pid(process_monitor, worker.first, worker.second);

                                          // now wait for it to be ready
                                          return worker.second->async_wait_launched(use_continuation);
                                      });
                           });
                },
                token,
                std::forward<Args>(args)...);
        }

        /** Broadcast a message to all agents, once they are ready.
         *
         * This will cache messages for not-ready agents, and send them when
         * they become ready.
         * @tparam MessageType IPC message type
         * @tparam CompletionToken Continuation or callback to handle the result
         * @param message Message to send
         * @param token Completion token instance
         * @return Continuation or void if the CompletionToken is a callback
         */
        template<typename MessageType, typename CompletionToken>
        auto async_broadcast_when_ready(MessageType message, CompletionToken && token)
        {
            using namespace async::continuations;
            using message_type = std::decay_t<MessageType>;
            static_assert(ipc::is_ipc_message_type_v<message_type>);

            return async_initiate_cont(
                [this, message = std::move(message)]() {
                    return start_on(strand) //
                         | iterate(agent_workers,
                                   [this, message = std::move(message)](auto it) -> polymorphic_continuation_t<> {
                                       auto & agent = it->second;
                                       if (agent.is_ready) {
                                           LOG_DEBUG("Sending broadcast message (%s) to agent process %d",
                                                     ipc::get_message_name(message).data(),
                                                     it->first);
                                           return agent.worker->async_send_message(message,
                                                                                   strand.context(),
                                                                                   use_continuation)
                                                | map_error();
                                       }

                                       LOG_DEBUG(
                                           "Agent process %d was not ready. Broadcast message [%s] will be cached",
                                           it->first,
                                           ipc::get_message_name(message).data());
                                       agent.cached_messages.push_back(message);
                                       return {};
                                   });
                },
                std::forward<CompletionToken>(token));
        }

    private:
        struct agent_worker_state_t {
            agent_worker_state_t(std::shared_ptr<i_agent_worker_t> w) : worker {std::move(w)}, is_ready {false} {}

            std::shared_ptr<i_agent_worker_t> worker;
            std::deque<ipc::all_message_types_variant_t> cached_messages;
            bool is_ready;
        };

        /** Monitor the agent process for termination */
        template<typename ProcessMonitor>
        static void observe_agent_pid(ProcessMonitor & process_monitor,
                                      pid_t pid,
                                      std::shared_ptr<i_agent_worker_t> const & worker)
        {
            using namespace async::continuations;

            spawn("observe_agent_pid",
                  process_monitor.async_monitor_forked_pid(pid, use_continuation) //
                      | then([&process_monitor, pid, worker](auto uid) {
                            auto repeat_flag = std::make_shared<bool>(true);
                            return repeatedly(
                                [repeat_flag]() { return *repeat_flag; },
                                [&process_monitor, pid, uid, worker, repeat_flag]() {
                                    LOG_DEBUG("Waiting for event %d", pid);

                                    return process_monitor.async_wait_event(uid, use_continuation) //
                                         | then([pid, worker, repeat_flag](auto ec, auto event) {
                                               if (ec) {
                                                   LOG_WARNING("unexpected error reported for process %d (%s)",
                                                               pid,
                                                               ec.message().c_str());
                                               }

                                               switch (event.state) {
                                                   case async::proc::ptrace_process_state_t::no_such_process:
                                                   case async::proc::ptrace_process_state_t::terminated_exit:
                                                   case async::proc::ptrace_process_state_t::terminated_signal: {
                                                       // notify of the signal
                                                       LOG_DEBUG(
                                                           "Notifying worker that agent process %d (%p) terminated.",
                                                           pid,
                                                           worker.get());
                                                       worker->on_sigchild();

                                                       *repeat_flag = false;
                                                       return;
                                                   }

                                                   case async::proc::ptrace_process_state_t::attached:
                                                   case async::proc::ptrace_process_state_t::attaching:
                                                   default: {
                                                       LOG_TRACE("ignoring unexpected event state %s::%s",
                                                                 to_cstring(event.type),
                                                                 to_cstring(event.state));
                                                       return;
                                                   }
                                               }
                                           });
                                });
                        }));
        }

        Parent & parent;
        i_agent_spawner_t & hi_priv_spawner;
        i_agent_spawner_t & lo_priv_spawner;
        boost::asio::io_context & io_context;
        boost::asio::io_context::strand strand {io_context};
        std::map<pid_t, agent_worker_state_t> agent_workers {};
        std::deque<ipc::all_message_types_variant_t> delayed_broadcasts {};

        std::size_t sigalarm_counter = 0;

        bool created_any = false;
        bool terminated = false;

        /** terminate the worker loop */
        void terminate()
        {
            LOG_FINE("All agents exited, terminating");
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

        /** Construct the state observer object for some agent process. This function will
         * process state changes, and update this class's state as appropriate. It will
         * also notify the agent-process-started handler at the correct time. */
        i_agent_worker_t::state_change_observer_t make_state_observer()
        {
            using namespace async::continuations;

            return [this](auto pid, auto /*old_state*/, auto new_state) mutable {
                // transition to terminated
                if (new_state == i_agent_worker_t::state_t::terminated) {
                    spawn("Handle agent terminated notification operation",
                          start_on(strand) //
                              | then([this, pid]() {
                                    LOG_DEBUG("Received agent terminated notification for agent process %d", pid);
                                    // remove it
                                    agent_workers.erase(pid);
                                    // stop if no more agents
                                    on_strand_check_terminated();
                                }));
                }
                else if (new_state == i_agent_worker_t::state_t::ready) {
                    spawn("Handle agent ready notification operation",
                          start_on(strand) //
                              | then([this, pid]() -> polymorphic_continuation_t<> {
                                    auto it = agent_workers.find(pid);
                                    if (it == agent_workers.end()) {
                                        LOG_WARNING("Unknown agent PID: %d", pid);
                                        return {};
                                    }

                                    auto & agent = it->second;
                                    agent.is_ready = true;

                                    // Send all the cached messages asynchronously, stopping when they're
                                    // all sent or when the agent is terminated
                                    return start_with(false, pid) //
                                         | loop(
                                               [this](auto cheap_exit, auto pid) -> //
                                               polymorphic_continuation_t<bool, bool, pid_t> {
                                                   if (cheap_exit) {
                                                       return start_with(false, false, pid);
                                                   }

                                                   return start_on(strand) //
                                                        | then([this, pid]() {
                                                              LOG_DEBUG("Looking for pid %d", pid);
                                                              auto it = agent_workers.find(pid);
                                                              if (it == agent_workers.end()) {
                                                                  LOG_DEBUG("pid not found");
                                                                  return start_with(it != agent_workers.end(),
                                                                                    false,
                                                                                    pid);
                                                              }

                                                              LOG_DEBUG("Cached messages empty: %d",
                                                                        it->second.cached_messages.empty());
                                                              return start_with(!it->second.cached_messages.empty(),
                                                                                false,
                                                                                pid);
                                                          });
                                               },
                                               [this](auto /*cheap_exit*/, auto pid) {
                                                   return start_on(strand)      //
                                                        | then([this, pid]() -> //
                                                               polymorphic_continuation_t<bool, pid_t> {
                                                                   auto it = agent_workers.find(pid);
                                                                   if (it == agent_workers.end()) {
                                                                       LOG_DEBUG("Not sending cached message: agent "
                                                                                 "was terminated");
                                                                       // Agent has been terminated and won't be coming
                                                                       // back, so skip the map lookup in the predicate
                                                                       return start_with(true, pid);
                                                                   }

                                                                   auto & agent = it->second;
                                                                   if (agent.cached_messages.empty()) {
                                                                       // Now that the agent is marked as ready, new
                                                                       // messages will be sent immediately, so we
                                                                       // can use the cheap exit as cached_messages
                                                                       // won't be used again
                                                                       return start_with(true, pid);
                                                                   }

                                                                   auto message =
                                                                       std::move(agent.cached_messages.front());
                                                                   agent.cached_messages.pop_front();

                                                                   return std::visit(
                                                                       [&](auto msg)
                                                                           -> polymorphic_continuation_t<bool, pid_t> {
                                                                           LOG_DEBUG("Sending cached broadcast message "
                                                                                     "(%s) to agent process %d",
                                                                                     ipc::get_message_name(msg).data(),
                                                                                     pid);

                                                                           return agent.worker->async_send_message(
                                                                                      std::move(msg),
                                                                                      strand.context(),
                                                                                      use_continuation)
                                                                                | map_error() //
                                                                                | then([pid]() {
                                                                                      return start_with(false, pid);
                                                                                  });
                                                                       },
                                                                       message);
                                                               });
                                               })
                                         | then([](auto...) {}); // Consume the loop output
                                }));
                }
            };
        }
    };
}
