/* Copyright (C) 2022-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "Configuration.h"
#include "Logging.h"
#include "Time.h"
#include "agents/agent_environment.h"
#include "agents/perf/async_perf_ringbuffer_monitor.hpp"
#include "agents/perf/cpufreq_counter.h"
#include "agents/perf/events/event_binding_manager.hpp"
#include "agents/perf/events/event_bindings.hpp"
#include "agents/perf/events/perf_activator.hpp"
#include "agents/perf/events/types.hpp"
#include "agents/perf/perf_buffer_consumer.h"
#include "agents/perf/perf_capture_events_helper.hpp"
#include "apc/misc_apc_frame_ipc_sender.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/continuation.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "async/proc/async_exec.hpp"
#include "async/proc/async_process.hpp"
#include "async/proc/async_read_proc_maps.h"
#include "async/proc/async_read_proc_sys_dependencies.h"
#include "async/proc/async_wait_for_process.h"
#include "async/proc/process_monitor.hpp"
#include "ipc/messages.h"
#include "ipc/raw_ipc_channel_sink.h"
#include "lib/FsEntry.h"
#include "lib/error_code_or.hpp"
#include "lib/forked_process.h"
#include "linux/proc/ProcessChildren.h"

#include <memory>

#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/date_time/posix_time/posix_time_duration.hpp>
#include <boost/system/error_code.hpp>

namespace agents::perf {
    /**
     * Provides various "leaf" operations for perf_capture_t
     */
    template<typename PerfCaptureEventsHelper = perf_capture_events_helper_t<>,
             typename AsyncRingBufferMonitor =
                 async_perf_ringbuffer_monitor_t<perf_activator_t,
                                                 perf_buffer_consumer_t,
                                                 typename PerfCaptureEventsHelper::stream_descriptor_t>,
             typename ProcessMonitor = async::proc::process_monitor_t>
    class perf_capture_helper_t
        : public std::enable_shared_from_this<
              perf_capture_helper_t<PerfCaptureEventsHelper, AsyncRingBufferMonitor, ProcessMonitor>> {
    public:
        using perf_capture_events_helper_t = PerfCaptureEventsHelper;
        using async_perf_ringbuffer_monitor_t = AsyncRingBufferMonitor;
        using stream_descriptor_t = typename perf_capture_events_helper_t::stream_descriptor_t;
        using process_monitor_t = ProcessMonitor;

        /** Constructor */
        perf_capture_helper_t(std::shared_ptr<perf_capture_configuration_t> conf,
                              boost::asio::io_context & context,
                              process_monitor_t & process_monitor,
                              agent_environment_base_t::terminator terminator,
                              std::shared_ptr<async_perf_ringbuffer_monitor_t> aprm,
                              perf_capture_events_helper_t && pceh,
                              std::shared_ptr<ICpuInfo> cpu_info,
                              std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink)
            : configuration(std::move(conf)),
              strand(context),
              process_monitor(process_monitor),
              terminator(std::move(terminator)),
              cpu_info(std::move(cpu_info)),
              ipc_sink(std::move(ipc_sink)),
              misc_apc_frame_ipc_sender(std::make_shared<apc::misc_apc_frame_ipc_sender_t>(this->ipc_sink)),
              async_perf_ringbuffer_monitor(std::move(aprm)),
              perf_capture_events_helper(std::move(pceh))
        {
        }

        /** @return True if the captured events are enable-on-exec, rather than started manually */
        [[nodiscard]] bool is_enable_on_exec() const { return perf_capture_events_helper.is_enable_on_exec(); }

        /** @return True if configured counter groups include the SPE group */
        [[nodiscard]] bool has_spe() const { return perf_capture_events_helper.has_spe(); }

        /** @return True if terminate was requested */
        [[nodiscard]] bool is_terminate_requested() const
        {
            return terminate_requested || async_perf_ringbuffer_monitor->is_terminate_requested();
        }

        /** Tell the events helper to mark the EBM as started so that events are enabled when the cores come online*/
        void enable_counters()
        {
            // tell the EBM that capture started
            perf_capture_events_helper.set_capture_started();
            // start the ringbuffer timer
            async_perf_ringbuffer_monitor->start_timer();
        }

        /** Spawn an observer of the one-shot-full event */
        void observe_one_shot_event()
        {
            using namespace async::continuations;

            // wait for one-shot mode terminate event
            spawn("one-shot mode waiter",
                  async_perf_ringbuffer_monitor->async_wait_one_shot_full(use_continuation),
                  [st = this->shared_from_this()](bool) {
                      LOG_DEBUG("Stopping due to one shot mode");
                      st->terminate(false);
                  });
        }

        /** Mark capture as started */
        template<typename CompletionToken>
        auto async_notify_start_capture(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = this->shared_from_this()]() -> polymorphic_continuation_t<> {
                    if (st->is_terminate_requested()) {
                        return {};
                    }

                    // trigger the capture to start
                    return st->ipc_sink->async_send_message(ipc::msg_capture_started_t {}, use_continuation)
                         | then([st](auto const & ec, auto const & /*msg*/) {
                               if (ec) {
                                   st->terminate(false);
                               }
                           });
                },
                std::forward<CompletionToken>(token));
        }

        /** Start pid monitoring */
        template<typename CompletionToken>
        auto async_start_pids(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = this->shared_from_this()]() -> polymorphic_continuation_t<> {
                    if (st->is_terminate_requested()) {
                        return {};
                    }

                    LOG_FINE("Starting pid monitoring...");

                    // start any pids we are monitoring
                    return start_on(st->strand) //
                         | then([st]() -> polymorphic_continuation_t<> {
                               // prepare the event trackers
                               auto result = st->perf_capture_events_helper.prepare_all_pid_trackers(
                                   [st]() { return st->is_terminate_requested(); });

                               // terminate on failure
                               if (!result) {
                                   if (!st->is_terminate_requested()) {
                                       st->terminate(false);
                                   }
                                   return {};
                               }

                               // and send all the mappings (asynchronously)
                               spawn("process key->id mapping task",
                                     st->misc_apc_frame_ipc_sender->async_send_keys_frame(result->id_to_key_mappings,
                                                                                          use_continuation)
                                         | map_error(),
                                     [st](bool failed) {
                                         if (failed) {
                                             st->terminate(false);
                                         }
                                     });

                               auto paused_pids = std::move(result->paused_pids);

                               // then track buffer
                               return start_on(st->strand.context()) //
                                    | then([st, result = std::move(result)]() mutable {
                                          st->async_perf_ringbuffer_monitor->add_additional_event_fds(
                                              std::move(result->event_fds),
                                              std::move(result->supplimentary_event_fds));
                                      }) //
                                    // now possibly start the events
                                    | then([st, paused_pids = std::move(paused_pids)]() mutable {
                                          // ensure that the pids are resumed after we return
                                          std::map<pid_t, lnx::sig_continuer_t> pp {std::move(paused_pids)};
                                          // then start the events
                                          if (!st->perf_capture_events_helper.start_all_pid_trackers()) {
                                              LOG_DEBUG("start_all_pid_trackers returned false, terminating");
                                              st->terminate(false);
                                          }

                                          // finally, spawn something to monitor for new pids
                                          spawn_pid_monitor(st);
                                      });
                           });
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Wait for all capture data to be transmitted and the capture to end
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_wait_terminated(CompletionToken && token)
        {
            // currently, only requires waiting for the ringbuffer to drain so just forward the request
            return async_perf_ringbuffer_monitor->async_wait_terminated(std::forward<CompletionToken>(token));
        }

        /**
         * For a single cpu, read the initial counter values for any counters that must be polled on start up.
         *
         * Currently, this is only for the cpu_frequency counter.
         *
         * @param monotonic_start The capture start timestamp (in CLOCK_MONOTONIC_RAW)
         * @param cpu_no The number of the cpu for which counters should be polled
         * @param token The completion token for the async operation
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_read_initial_counter_value(std::uint64_t monotonic_start,
                                                            int cpu_no,
                                                            CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = this->shared_from_this(), monotonic_start, cpu_no]() {
                    return start_on(st->strand) //
                         | then([st, monotonic_start, cpu_no]() -> polymorphic_continuation_t<> {
                               //read the counter
                               auto counter =
                                   read_cpu_frequency(cpu_no,
                                                      *st->cpu_info,
                                                      st->configuration->cluster_keys_for_cpu_frequency_counter);

                               // skip if no value
                               if (!counter) {
                                   return {};
                               }

                               // send the counter frame
                               std::array<apc::perf_counter_t, 1> counter_values {{
                                   *counter,
                               }};

                               return st->misc_apc_frame_ipc_sender->async_send_perf_counters_frame(
                                          monotonic_delta_now(monotonic_start),
                                          counter_values,
                                          use_continuation) //
                                    | map_error();
                           });
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * For all cpus, read the initial counter values for any counters that must be polled on start up.
         *
         * @param monotonic_start The capture start timestamp (in CLOCK_MONOTONIC_RAW)
         * @param token The completion token for the async operation
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_read_initial_counter_values(std::uint64_t monotonic_start, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = this->shared_from_this(), monotonic_start]() {
                    return start_on(st->strand) //
                         | then([st, monotonic_start]() {
                               return iterate(std::size_t {0},
                                              st->cpu_info->getNumberOfCores(),
                                              [st, monotonic_start](auto cpu_no) {
                                                  return st->async_read_initial_counter_value(
                                                      monotonic_start,
                                                      cpu_no,
                                                      async::continuations::use_continuation);
                                              });
                           });
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Poll all currently running processes/threads in /proc and write their basic properties (pid, tid, comm, exe)
         * into the capture
         *
         * @param token The completion token for the async operation
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_read_process_properties(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = this->shared_from_this()]() {
                    return async::async_read_proc_sys_dependencies(
                               st->strand,
                               st->misc_apc_frame_ipc_sender,
                               [sw = isCaptureOperationModeSystemWide(
                                    st->configuration->session_data.capture_operation_mode),
                                pids = st->perf_capture_events_helper.get_monitored_pids(),
                                gatord_pids = st->perf_capture_events_helper.get_monitored_gatord_pids()](int pid,
                                                                                                          int tid) {
                                   return sw || (pids.count(pid) > 0) || (pids.count(tid) > 0)
                                       || (gatord_pids.count(pid) > 0) || (gatord_pids.count(tid) > 0);
                               },
                               use_continuation) //
                         | map_error();
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Poll all currently running processes/threads in /proc and write their `maps` file contents into the capture
         *
         * @param token The completion token for the async operation
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_read_process_maps(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = this->shared_from_this()]() {
                    return async::async_read_proc_maps(
                               st->strand,
                               st->misc_apc_frame_ipc_sender,
                               [sw = isCaptureOperationModeSystemWide(
                                    st->configuration->session_data.capture_operation_mode),
                                pids = st->perf_capture_events_helper.get_monitored_pids(),
                                gatord_pids = st->perf_capture_events_helper.get_monitored_gatord_pids()](int pid) {
                                   return sw || (pids.count(pid) > 0) || (gatord_pids.count(pid) > 0);
                               },
                               use_continuation)
                         | map_error();
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Read the kallsyms file and write into the capture
         *
         * @param token The completion token for the async operation
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_read_kallsyms(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = this->shared_from_this()]() -> polymorphic_continuation_t<> {
                    auto kallsyms = lib::FsEntry::create("/proc/kallsyms");

                    if ((!kallsyms.exists()) || (!kallsyms.canAccess(true, false, false))) {
                        return {};
                    }

                    auto contents = kallsyms.readFileContents();
                    if (contents.empty()) {
                        return {};
                    }

                    return st->misc_apc_frame_ipc_sender->async_send_kallsyms_frame(std::move(contents),
                                                                                    use_continuation)
                         | map_error();
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Send a core name apc frame
         *
         * @param cpu_no The cpu number of the core to send the message for
         * @param token The completion token for the async operation
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_send_core_name_msg(int cpu_no, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = this->shared_from_this(), cpu_no]() {
                    return start_on(st->strand) //
                         | then([st, cpu_no]() -> polymorphic_continuation_t<> {
                               // Don't send information on a cpu we know nothing about
                               auto const cpu_ids = st->cpu_info->getCpuIds();

                               if ((cpu_no < 0) || (std::size_t(cpu_no) >= cpu_ids.size())) {
                                   return {};
                               }

                               const int cpu_id = cpu_ids[cpu_no];
                               if (cpu_id == -1) {
                                   return {};
                               }

                               // we use cpuid lookup here for look up rather than clusters because it maybe a cluster
                               // that wasn't known at start up
                               auto it = st->configuration->cpuid_to_core_name.find(cpu_id);
                               if (it != st->configuration->cpuid_to_core_name.end()) {
                                   return st->misc_apc_frame_ipc_sender->async_send_core_name(cpu_no,
                                                                                              cpu_id,
                                                                                              it->second,
                                                                                              use_continuation)
                                        | map_error();
                               }

                               // create the core name string
                               lib::printf_str_t<32> buf {"Unknown (0x%.3x)", cpu_id};
                               return st->misc_apc_frame_ipc_sender->async_send_core_name(cpu_no,
                                                                                          cpu_id,
                                                                                          std::string(buf),
                                                                                          use_continuation)
                                    | map_error();
                           });
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Send the initial summary frame
         *
         * @param token The completion token for the async operation
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_send_summary_frame(std::uint64_t monotonic_raw_start,
                                                    std::uint64_t monotonic_start,
                                                    CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = this->shared_from_this(), monotonic_raw_start, monotonic_start]() {
                    return start_on(st->strand) //
                         | then([st, monotonic_raw_start, monotonic_start]() -> polymorphic_continuation_t<> {
                               auto state = create_perf_driver_summary_state(
                                   st->configuration->perf_config,
                                   monotonic_raw_start,
                                   monotonic_start,
                                   isCaptureOperationModeSystemWide(
                                       st->configuration->session_data.capture_operation_mode));

                               if (!state) {
                                   return start_with(boost::asio::error::make_error_code(
                                              boost::asio::error::basic_errors::operation_aborted))
                                        | map_error();
                               }

                               return start_with()
                                    // send the summary
                                    | st->misc_apc_frame_ipc_sender->async_send_summary_message(std::move(*state),
                                                                                                use_continuation) //
                                    | map_error()
                                    // send core names
                                    | iterate(std::size_t {0}, st->cpu_info->getNumberOfCores(), [st](int cpu_no) {
                                          return st->async_send_core_name_msg(cpu_no, use_continuation);
                                      });
                           });
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Rescan for any changes to the CPU info, sending the appropriate core name message
         *
         * @param cpu_no The core for which to enable events
         * @param token The completion token for the async operation
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_rescan_cpu_info(int cpu_no, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = this->shared_from_this(), cpu_no]() {
                    return start_on(st->strand) //
                         | then([st, cpu_no]() {
                               // rescan the ids from proc / sysfs
                               st->cpu_info->updateIds(true);
                               // and update the capture
                               return st->async_send_core_name_msg(cpu_no, use_continuation);
                           });
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Output any cpu online/offline event messages as part of a state change
         *
         * @param monotonic_start The capture start timestamp (in CLOCK_MONOTONIC_RAW)
         * @param cpu_no The core for which to enable events
         * @param online True if the core was online, false if it was offline
         * @param token The completion token for the async operation
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_core_state_change_msg(std::uint64_t monotonic_start,
                                                       int cpu_no,
                                                       bool online,
                                                       CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = this->shared_from_this(), monotonic_start, cpu_no, online]() {
                    auto monotonic_delta = monotonic_delta_now(monotonic_start);

                    return
                        // store the entry in the capture
                        st->misc_apc_frame_ipc_sender->async_send_cpu_online_frame(monotonic_delta,
                                                                                   cpu_no,
                                                                                   online,
                                                                                   use_continuation)
                        | map_error()
                        // and tell the shell
                        | st->ipc_sink->async_send_message(
                            ipc::msg_cpu_state_change_t {{monotonic_delta, cpu_no, online}},
                            use_continuation)
                        | map_error_and_discard();
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Activate all the perf events for a given core, and start observing them in the ring buffer,
         * but do not necessarily enable the events.
         *
         * Events will only be enabled if `start_counters` was previously called, or we are `enable_on_exec` and
         * `co_exec_child` was previously completed.
         *
         * @param cpu_no The core for which to enable events
         * @param token The completion token for the async operation
         * @return The async result will be a bool indicating true for successful onlining, and false for core is offline.
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_prepare_per_core_events([[maybe_unused]] int cpu_no, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = this->shared_from_this(), cpu_no]() {
                    return start_on(st->strand) //
                         | then([st, cpu_no]() -> polymorphic_continuation_t<bool> {
                               // prepare the events
                               auto error_or_result =
                                   st->perf_capture_events_helper.core_online_prepare(core_no_t(cpu_no),
                                                                                      st->get_cluster_id(cpu_no));

                               if (auto const * error = lib::get_error(error_or_result)) {
                                   return start_with(*error, false) //
                                        | map_error();
                               }

                               auto result = lib::get_value(std::move(error_or_result));

                               // send all the mappings (asynchronously)
                               spawn("core key->id mapping task",
                                     st->misc_apc_frame_ipc_sender->async_send_keys_frame(result.mappings,
                                                                                          use_continuation)
                                         | map_error(),
                                     [st](bool failed) {
                                         if (failed) {
                                             st->terminate(false);
                                         }
                                     });

                               // then track buffer
                               return st->async_perf_ringbuffer_monitor->async_add_ringbuffer(
                                          cpu_no,
                                          std::move(result.event_fds),
                                          std::move(result.supplimentary_event_fds),
                                          std::move(result.mmap_ptr),
                                          use_continuation)
                                    | map_error()
                                    // now possibly start the events
                                    | then([st, cpu_no, paused_pids = std::move(result.paused_pids)]() mutable {
                                          // ensure that the pids are resumed after we return
                                          std::map<pid_t, lnx::sig_continuer_t> pp {std::move(paused_pids)};
                                          // start the core
                                          return st->perf_capture_events_helper.core_online_start(core_no_t(cpu_no));
                                      })
                                    | unpack_tuple() //
                                    | map_error();
                           });
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Deactivate all the perf events for a given core and stop observing them.
         *
         * @param cpu_no The core for which to enable events
         * @param token The completion token for the async operation
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_remove_per_core_events([[maybe_unused]] int cpu_no, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = this->shared_from_this(), cpu_no]() {
                    return start_on(st->strand)                                                                     //
                         | then([st, cpu_no]() { st->perf_capture_events_helper.core_offline(core_no_t(cpu_no)); }) //
                         | st->async_perf_ringbuffer_monitor->await_mmap_removed(cpu_no, use_continuation);
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Launch any android package and then poll for the process to start.
         * Once the process is detected as running, the list of tracked pids is updated.
         *
         * @param token The completion token for the async operation
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_wait_for_process(CompletionToken && token)
        {
            using namespace async::continuations;
            using namespace std::chrono_literals;

            return async_initiate_cont(
                [st = this->shared_from_this()]() mutable {
                    return start_on(st->strand) //
                         | then([=]() mutable {
                               st->waiter = async::make_async_wait_for_process(st->strand.context(),
                                                                               st->configuration->wait_process,
                                                                               st->configuration->android_pkg);
                           })
                         | st->ipc_sink->async_send_message(ipc::msg_exec_target_app_t {}, use_continuation)
                         | map_error_and_discard() //
                         | then([=]() { return st->waiter->start(1ms, use_continuation); })
                         | then([=](auto ec, auto pids) mutable {
                               st->waiter.reset();

                               LOG_DEBUG("DETECTED APP PIDS: (ec=%s)", ec.message().c_str());
                               for (auto pid : pids) {
                                   LOG_DEBUG("    %d", pid);
                               }

                               if (ec) {
                                   return ec;
                               }

                               st->perf_capture_events_helper.add_stoppable_pids(pids);

                               return boost::system::error_code {};
                           })
                         | map_error();
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Fork (but not exec) the child process. The process is forked so that its pid is known
         * and events may be attached to it. The process is only exec'd once the capture is
         * ready to start.
         *
         * @param token The completion token for the async operation
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_fork_process(CompletionToken && token)
        {
            using namespace async::continuations;
            return async_initiate_cont(
                [st = this->shared_from_this()]() mutable {
                    return start_on(st->strand) //
                         | then([st]() mutable -> polymorphic_continuation_t<> {
                               auto config = st->configuration;
                               if ((!config) || (!config->command)) {
                                   return {};
                               }
                               auto & command = *(config->command);
                               LOG_INFO("Starting command: %s...", command.command.c_str());
                               return async::proc::async_create_process(st->process_monitor,
                                                                        st->strand.context(),
                                                                        async::proc::async_exec_args_t {
                                                                            command.command,
                                                                            command.args,
                                                                            command.cwd,
                                                                            command.uid,
                                                                            command.gid,
                                                                        },
                                                                        async::proc::discard_ioe,
                                                                        async::proc::log_oe,
                                                                        async::proc::log_oe,
                                                                        use_continuation) //
                                    | map_error()                                         //
                                    | post_on(st->strand)                                 //
                                    | then([st](auto command) {
                                          LOG_DEBUG("Successfully forked child process");
                                          // save it for later
                                          st->forked_command = command;

                                          // add its pid to the list of monitored pids
                                          st->perf_capture_events_helper.add_monitored_pid(command->get_pid());
                                      });
                           });
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Prepare any process that should be profiled; at the end of this operation the list
         * of tracked pids will contains one or more values prepresenting the processes to
         * profile.
         * When making a system-wide capture (without --app/--pid etc), or for where the pids
         * are already specified (with --pid) then this operation is a nop.
         *
         * @param token The completion token for the async operation
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_prepare_process(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = this->shared_from_this()]() {
                    return start_with()
                         | do_if([st]() { return !st->configuration->wait_process.empty(); }, //
                                 [st]() { return st->async_wait_for_process(use_continuation); })
                         | do_if([st]() { return st->configuration->command.has_value(); }, //
                                 [st]() { return st->async_fork_process(use_continuation); });
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Exec the child process forked previously for --app
         *
         * @param token The completion token for the async operation
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_exec_child(CompletionToken && token)
        {
            using namespace async::continuations;
            return async_initiate_cont(
                [st = this->shared_from_this()]() {
                    return start_on(st->strand) //
                         | then([st]() {
                               // clear stopped_tids which will resume any stopped pids
                               st->perf_capture_events_helper.clear_stopped_tids();

                               // and exec the forked process
                               auto fc = st->forked_command;
                               if (!fc) {
                                   return;
                               }

                               // spawn the termination observer
                               spawn("Waiting for process termination",
                                     async::proc::async_run_to_completion(fc, use_continuation)
                                         | then([st, forked_pid = fc->get_pid()](auto ec, bool by_signal, int status) {
                                               if (ec) {
                                                   LOG_WARNING("Exec monitor failed with error %s",
                                                               ec.message().c_str());
                                               }
                                               else if (by_signal) {
                                                   LOG_ERROR("Command exited with signal %d", status);
                                               }
                                               else if (status != 0) {
                                                   LOG_ERROR("Command exited with code %d", status);
                                               }
                                               else {
                                                   LOG_DEBUG("Command exited with code 0");
                                               }

                                               if ((!by_signal)
                                                   && (status == lib::forked_process_t::failure_exec_invalid)) {
                                                   LOG_ERROR(
                                                       "Failed to run command %s: Permission denied or is a directory",
                                                       st->configuration->command->command.c_str());
                                                   st->on_command_exited(forked_pid, true);
                                               }
                                               else if ((!by_signal)
                                                        && (status == lib::forked_process_t::failure_exec_not_found)) {
                                                   LOG_ERROR("Failed to run command %s: Command not found",
                                                             st->configuration->command->command.c_str());
                                                   st->on_command_exited(forked_pid, true);
                                               }
                                               else {
                                                   st->on_command_exited(forked_pid, false);
                                               }
                                           }),
                                     [st](bool) { st->terminate(true); });
                           });
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Tell shell that the agent is ready to start
         *
         * @param token The completion token for the async operation
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_notify_agent_ready(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = this->shared_from_this()]() {
                    return start_on(st->strand) //
                         | then([st]() {
                               //let the shell know we are ready
                               auto const & monitored_pids = st->perf_capture_events_helper.get_monitored_pids();
                               return st->ipc_sink->async_send_message(
                                          ipc::msg_capture_ready_t {std::vector<pid_t>(monitored_pids.begin(), //
                                                                                       monitored_pids.end())},
                                          use_continuation) //
                                    | map_error_and_discard();
                           });
                },
                std::forward<CompletionToken>(token));
        }

        /** Cancel any outstanding asynchronous operations that need special handling. */
        void terminate(bool defer = false)
        {
            boost::asio::post(strand, [defer, st = this->shared_from_this()]() {
                // delay to use when deferring shutdown
                constexpr auto defer_delay_ms = boost::posix_time::milliseconds(1000);

                // only perform the terminate once
                auto timer = std::exchange(st->terminate_delay_timer, nullptr);
                if (timer == nullptr) {
                    return;
                }

                // termination handler, which may be defered
                auto handler = [timer, st](boost::system::error_code const & ec) mutable {
                    if (ec != boost::asio::error::operation_aborted) {
                        LOG_FATAL("Terminating pid monitoring... terminating.");
                        timer->cancel();
                        timer.reset();

                        auto w = st->waiter;
                        if (w) {
                            w->cancel();
                        }

                        st->perf_capture_events_helper.clear_stopped_tids();

                        auto fc = st->forked_command;
                        if (fc) {
                            fc->abort();
                        }

                        st->async_perf_ringbuffer_monitor->terminate();

                        st->terminator();
                    }
                };

                // defer the terminate() call to allow the async_perf_ringbuffer_monitor to receive any closed() events for the event fds it monitors
                if (defer) {
                    LOG_FATAL("Terminating pid monitoring... starting termination countdown.");
                    timer->expires_from_now(defer_delay_ms);
                    timer->async_wait(std::move(handler));
                }
                // otherwise, call the handler directly
                else {
                    handler({});
                }
            });
        }

        void on_perf_error()
        {
            using namespace async::continuations;

            spawn("perf error handler",
                  ipc_sink->async_send_message(
                      ipc::msg_capture_failed_t {ipc::capture_failed_reason_t::wait_for_cores_ready_failed},
                      use_continuation),
                  [st = this->shared_from_this()](bool) { st->terminate(false); });
        }

    private:
        static void spawn_pid_monitor(std::shared_ptr<perf_capture_helper_t> st)
        {
            using namespace async::continuations;

            switch (st->configuration->session_data.capture_operation_mode) {
                case perf_capture_configuration_t::capture_operation_mode_t::application_poll:
                    break;
                case perf_capture_configuration_t::capture_operation_mode_t::application_no_inherit:
                case perf_capture_configuration_t::capture_operation_mode_t::system_wide:
                case perf_capture_configuration_t::capture_operation_mode_t::application_inherit:
                case perf_capture_configuration_t::capture_operation_mode_t::application_experimental_patch:
                default:
                    return;
            }

            auto poll_delay_timer = std::make_shared<boost::asio::deadline_timer>(st->strand.context());

            spawn("process scanner",
                  repeatedly(
                      [st]() { return !st->is_terminate_requested(); }, //
                      [st, poll_delay_timer]() -> polymorphic_continuation_t<> {
                          LOG_DEBUG("SCANNING PIDS");

                          // perform the scan
                          auto error_or_result = st->perf_capture_events_helper.scan_for_new_tids();

                          if (boost::system::error_code const * error = lib::get_error(error_or_result)) {
                              LOG_ERROR("Got an error in process scanner: %s", error->what().c_str());
                              if (!st->is_terminate_requested()) {
                                  st->terminate(false);
                              }
                              return {};
                          }

                          auto result = lib::get_value(std::move(error_or_result));

                          // and send all the mappings (asynchronously)
                          if (!result.id_to_key_mappings.empty()) {
                              spawn("process key->id mapping task",
                                    st->misc_apc_frame_ipc_sender->async_send_keys_frame(result.id_to_key_mappings,
                                                                                         use_continuation)
                                        | map_error(),
                                    [st](bool failed) {
                                        if (failed) {
                                            st->terminate(false);
                                        }
                                    });
                          }

                          auto const any_new = !result.new_pids.empty();

                          // then track buffer
                          return start_on(st->strand.context()) //
                               | then([st,
                                       new_pids = std::move(result.new_pids),
                                       event_fds = std::move(result.event_fds),
                                       supplimentary_event_fds = std::move(result.supplimentary_event_fds)]() mutable {
                                     // add the events
                                     st->async_perf_ringbuffer_monitor->add_additional_event_fds(
                                         std::move(event_fds),
                                         std::move(supplimentary_event_fds));

                                     // now enable all
                                     return st->perf_capture_events_helper.enable_new_tids(new_pids);
                                 })
                               | map_error() //
                               | do_if([any_new]() { return any_new; },
                                       [st]() {
                                           return st->async_read_process_properties(use_continuation) //
                                                | st->async_read_process_maps(use_continuation);
                                       }) //
                               | then([poll_delay_timer]() {
                                     // delay to use when deferring scanning
                                     constexpr auto defer_delay_ms = boost::posix_time::milliseconds(100);

                                     poll_delay_timer->expires_from_now(defer_delay_ms);
                                     return poll_delay_timer->async_wait(use_continuation);
                                 })
                               | then([](auto /*ec*/) {
                                     // ignored ec
                                 });
                      }),
                  [st, poll_delay_timer](bool failed) {
                      if (failed) {
                          st->terminate(false);
                          poll_delay_timer->cancel();
                      }
                  });
        }

        std::shared_ptr<perf_capture_configuration_t> configuration;
        boost::asio::io_context::strand strand;
        process_monitor_t & process_monitor;
        agent_environment_base_t::terminator terminator;
        std::shared_ptr<ICpuInfo> cpu_info;
        std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink;
        std::shared_ptr<apc::misc_apc_frame_ipc_sender_t> misc_apc_frame_ipc_sender;
        std::shared_ptr<async::async_wait_for_process_t<boost::asio::io_context::executor_type>> waiter {};
        std::shared_ptr<async_perf_ringbuffer_monitor_t> async_perf_ringbuffer_monitor;
        std::shared_ptr<async::proc::async_process_t> forked_command;
        std::shared_ptr<boost::asio::deadline_timer> terminate_delay_timer {
            new boost::asio::deadline_timer(strand.context())};
        perf_capture_events_helper_t perf_capture_events_helper;
        bool terminate_requested {false};

        [[nodiscard]] cpu_cluster_id_t get_cluster_id(int cpu_no)
        {
            runtime_assert((cpu_no >= 0) && (std::size_t(cpu_no) < cpu_info->getNumberOfCores()), "Unexpected cpu no");

            return cpu_cluster_id_t(cpu_info->getClusterIds()[cpu_no]);
        }

        void on_command_exited(pid_t pid, bool exec_failed)
        {
            using namespace async::continuations;

            if (exec_failed) {
                spawn("command exited handler",
                      ipc_sink->async_send_message(
                          ipc::msg_capture_failed_t {ipc::capture_failed_reason_t::command_exec_failed},
                          use_continuation),
                      [st = this->shared_from_this()](bool) { st->terminate(false); });
            }
            else if (perf_capture_events_helper.remove_command_pid(pid)) {
                terminate(true);
            }
        }
    };
}
