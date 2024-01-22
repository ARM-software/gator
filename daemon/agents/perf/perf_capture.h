/* Copyright (C) 2022-2023 by Arm Limited. All rights reserved. */

#pragma once

#include "GetEventKey.h"
#include "SessionData.h"
#include "Time.h"
#include "agents/common/nl_cpu_monitor.h"
#include "agents/common/polling_cpu_monitor.h"
#include "agents/perf/capture_configuration.h"
#include "agents/perf/cpu_info.h"
#include "agents/perf/cpufreq_counter.h"
#include "agents/perf/events/event_binding_manager.hpp"
#include "agents/perf/events/perf_activator.hpp"
#include "agents/perf/perf_capture_cpu_monitor.h"
#include "agents/perf/perf_capture_helper.h"
#include "agents/perf/perf_driver_summary.h"
#include "agents/perf/sync_generator.h"
#include "apc/misc_apc_frame_ipc_sender.h"
#include "apc/summary_apc_frame_utils.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/continuation.h"
#include "async/continuations/operations.h"
#include "async/continuations/stored_continuation.h"
#include "async/continuations/use_continuation.h"
#include "ipc/messages.h"
#include "ipc/raw_ipc_channel_sink.h"
#include "lib/Assert.h"
#include "lib/Utils.h"

#include <memory>
#include <optional>
#include <set>

#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/io_context_strand.hpp>
#include <boost/system/error_code.hpp>

namespace agents::perf {

    /**
     * Manages the perf capture process.
     */
    class perf_capture_t : public std::enable_shared_from_this<perf_capture_t> {
    public:
        using perf_capture_helper_t = agents::perf::perf_capture_helper_t<>;
        using async_perf_ringbuffer_monitor_t = typename perf_capture_helper_t::async_perf_ringbuffer_monitor_t;
        using perf_capture_events_helper_t = typename perf_capture_helper_t::perf_capture_events_helper_t;
        using event_binding_manager_t = typename perf_capture_events_helper_t::event_binding_manager_t;
        using process_monitor_t = typename perf_capture_helper_t::process_monitor_t;

        /**
         * Construct a capture object from the provided configuration
         */
        static std::shared_ptr<perf_capture_t> create(boost::asio::io_context & context,
                                                      process_monitor_t & process_monitor,
                                                      std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink,
                                                      agent_environment_base_t::terminator terminator,
                                                      std::shared_ptr<perf_capture_configuration_t> configuration)
        {
            return std::make_shared<perf_capture_t>(context,
                                                    process_monitor,
                                                    std::move(ipc_sink),
                                                    std::move(terminator),
                                                    std::move(configuration));
        }

        /**
         * Construct a new perf capture object
         *
         * @param context The io context
         * @param sink The raw ipc channel sink
         * @param conf The configuration message contents
         */
        perf_capture_t(boost::asio::io_context & context,
                       process_monitor_t & process_monitor,
                       std::shared_ptr<ipc::raw_ipc_channel_sink_t> sink,
                       agent_environment_base_t::terminator terminator,
                       std::shared_ptr<perf_capture_configuration_t> conf)
            : strand(context),
              ipc_sink(std::move(sink)),
              configuration(std::move(conf)),
              perf_activator(std::make_shared<perf_activator_t>(configuration, context)),
              perf_capture_helper(std::make_shared<perf_capture_helper_t>(
                  configuration,
                  context,
                  process_monitor,
                  std::move(terminator),
                  std::make_shared<async_perf_ringbuffer_monitor_t>(
                      context,
                      ipc_sink,
                      perf_activator,
                      configuration->session_data.live_rate,
                      (configuration->session_data.one_shot ? configuration->session_data.total_buffer_size * MEGABYTES
                                                            : 0)),
                  perf_capture_events_helper_t(configuration,
                                               event_binding_manager_t(perf_activator,
                                                                       configuration->event_configuration,
                                                                       configuration->uncore_pmus,
                                                                       configuration->per_core_spe_type,
                                                                       configuration->perf_config.is_system_wide,
                                                                       configuration->enable_on_exec),
                                               std::move(configuration->pids)),
                  std::make_shared<cpu_info_t>(configuration),
                  ipc_sink)),
              perf_capture_cpu_monitor(std::make_shared<perf_capture_cpu_monitor_t>(context,
                                                                                    configuration->num_cpu_cores,
                                                                                    perf_capture_helper))
        {
        }

        /**
         * Called once at agent start *after* the capture configuration is received, prepares the agent
         * ready for capture.
         */
        template<typename CompletionToken>
        auto async_prepare(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = shared_from_this()]() {
                    // spawn a thread to poll for process to start or fork (but not exec the app we are launching)
                    // do not block on the continuation here, as it blocks the message loop
                    spawn("async_prepare",
                          st->perf_capture_helper->async_prepare_process(use_continuation)
                              // tell the shell gator that we are ready
                              | st->perf_capture_helper->async_notify_agent_ready(use_continuation)
                              //
                              | map_error_and_discard(),
                          [st](bool failed) {
                              // an error occured, terminate
                              if (failed) {
                                  st->perf_capture_helper->terminate(false);
                              }
                          });

                    return start_with();
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Called once the 'msg_start_t' message is received
         *
         * @param monotonic_start The monotonic start time
         */
        template<typename CompletionToken>
        auto async_on_received_start_message(std::uint64_t monotonic_start, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = shared_from_this(), monotonic_start]() {
                    return start_on(st->strand)
                         // send the summary frame
                         | st->perf_capture_helper->async_send_summary_frame(monotonic_start, use_continuation)
                         // start generating sync events and set misc ready parts for the helper
                         | then([st, monotonic_start]() {
                               st->perf_capture_helper->enable_counters();
                               st->perf_capture_helper->observe_one_shot_event();
                               st->start_sync_thread(monotonic_start);
                           })
                         // Start any pid monitoring
                         | st->perf_capture_helper->async_start_pids(use_continuation)
                         // bring online the core monitoring (after setting start_counters, as this enables the buffer monitor and tells the event binding set to activate in a started state)
                         | st->perf_capture_cpu_monitor->async_start_monitoring(monotonic_start, use_continuation)
                         // send any manually read initial counter values
                         | st->perf_capture_helper->async_read_initial_counter_values(monotonic_start, use_continuation)
                         // Spawn a separate async 'threads' to send various system-wide bits of data whilst the rest of the capture process continues
                         | then([st]() {
                               // the process initial properties
                               spawn_terminator(
                                   "process properies reader",
                                   st,
                                   st->perf_capture_helper->async_read_process_properties(use_continuation));

                               // and the contents of each process 'maps' file
                               spawn_terminator("process maps reader",
                                                st,
                                                st->perf_capture_helper->async_read_process_maps(use_continuation));

                               // and the contents of kallsyms file
                               spawn_terminator("kallsyms reader",
                                                st,
                                                st->perf_capture_helper->async_read_kallsyms(use_continuation));

                               // - finally, once the cores are all online, exec the child process
                               spawn_terminator(
                                   "waiting for cores to online",
                                   st,
                                   st->perf_capture_cpu_monitor->async_wait_for_all_cores_ready(use_continuation)
                                       | then([st](bool ready) -> polymorphic_continuation_t<> {
                                             if (!ready) {
                                                 return {};
                                             }

                                             // tell shell gator that the capture has started and then  exec the forked process
                                             return st->perf_capture_helper->async_notify_start_capture(
                                                        use_continuation)
                                                  | st->perf_capture_helper->async_exec_child(use_continuation);
                                         }));
                           });
                },
                std::forward<CompletionToken>(token));
        }

        /** Called to shutdown the capture */
        template<typename CompletionToken>
        auto async_shutdown(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = shared_from_this()]() {
                    return start_on(st->strand) //
                         | then([st]() mutable {
                               // trigger termination of various parts
                               st->perf_capture_cpu_monitor->terminate();

                               st->perf_capture_helper->terminate();

                               if (st->sync_thread) {
                                   st->sync_thread->terminate();
                               }

                               // then wait for the ringbuffers to be drained
                               return st->perf_capture_helper->async_wait_terminated(use_continuation);
                           });
                },
                std::forward<CompletionToken>(token));
        }

    private:
        using cpu_no_t = int;

        template<typename StateChain, typename... Args>
        static void spawn_terminator(char const * name,
                                     std::shared_ptr<perf_capture_t> const & shared_this,
                                     async::continuations::continuation_t<StateChain, Args...> && continuation)
        {
            spawn(name, std::move(continuation), [shared_this](bool failed) {
                if (failed) {
                    shared_this->perf_capture_helper->terminate();
                }
            });
        }

        boost::asio::io_context::strand strand;
        std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink;
        std::shared_ptr<perf_capture_configuration_t> configuration;
        std::shared_ptr<cpu_info_t> cpu_info {};
        std::shared_ptr<perf_activator_t> perf_activator {};
        std::shared_ptr<perf_capture_helper_t> perf_capture_helper {};
        std::unique_ptr<sync_generator> sync_thread {};
        std::shared_ptr<perf_capture_cpu_monitor_t> perf_capture_cpu_monitor {};
        static constexpr std::size_t MEGABYTES = 1024UL * 1024UL;

        /** @return True if the capture is terminated, false if not */
        [[nodiscard]] bool is_terminated() const { return perf_capture_cpu_monitor->is_terminated(); }

        /**
         * Launch the SPE sync thread
         *
         * @param monotonic_start The capture start timestamp (in CLOCK_MONOTONIC_RAW)
         */
        void start_sync_thread(std::uint64_t monotonic_start)
        {
            if (!is_terminated()) {
                runtime_assert(sync_thread == nullptr, "start_sync_thread called twice");

                sync_thread = sync_generator::create(configuration->perf_config.has_attr_clockid_support,
                                                     perf_capture_helper->has_spe(),
                                                     ipc_sink);

                if (sync_thread != nullptr) {
                    sync_thread->start(monotonic_start);
                }
            }
        };
    };
}
