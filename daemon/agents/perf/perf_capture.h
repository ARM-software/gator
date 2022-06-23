/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "CpuUtils.h"
#include "GetEventKey.h"
#include "ICpuInfo.h"
#include "SessionData.h"
#include "Time.h"
#include "agents/common/nl_cpu_monitor.h"
#include "agents/common/polling_cpu_monitor.h"
#include "agents/perf/capture_configuration.h"
#include "agents/perf/cpufreq_counter.h"
#include "agents/perf/perf_driver_summary.h"
#include "agents/perf/sync_generator.h"
#include "apc/misc_apc_frame_ipc_sender.h"
#include "apc/summary_apc_frame_utils.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/continuation.h"
#include "async/continuations/operations.h"
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

// TODO remove me
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace agents::perf {
    inline monotonic_delta_t monotonic_delta_now(std::uint64_t monotonic_start)
    {
        return monotonic_delta_t(getTime() - monotonic_start);
    }

    ////////

    /**
     * Manages the perf capture process.
     */
    class perf_capture_t : public std::enable_shared_from_this<perf_capture_t> {
    public:
        /**
         * Construct a capture object from the provided configuration
         */
        std::shared_ptr<perf_capture_t> create(boost::asio::io_context & context,
                                               std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink,
                                               perf_capture_configuration_t & configuration)
        {
            return std::make_shared<perf_capture_t>(context, std::move(ipc_sink), configuration);
        }

    private:
        /** Implements the ICpuInfo interface, providing a thin wrapper around the data received in the configuration message and allowing simple rescan of properties */
        class cpu_info_t : public ICpuInfo {
        public:
            explicit cpu_info_t(perf_capture_configuration_t & configuration) : configuration(configuration) {}

            [[nodiscard]] lib::Span<const int> getCpuIds() const override { return configuration.per_core_cpuids; }

            [[nodiscard]] lib::Span<const GatorCpu> getClusters() const override { return configuration.clusters; }

            [[nodiscard]] lib::Span<const int> getClusterIds() const override
            {
                return configuration.per_core_cluster_index;
            }

            [[nodiscard]] const char * getModelName() const override { return ""; }

            void updateIds(bool /*ignoreOffline*/) override
            {
                cpu_utils::readCpuInfo(true, false, configuration.per_core_cpuids);
                ICpuInfo::updateClusterIds(configuration.per_core_cpuids,
                                           configuration.clusters,
                                           configuration.per_core_cluster_index);
            }

        private:
            perf_capture_configuration_t & configuration;
        };

        using cpu_no_t = int;

        boost::asio::io_context::strand strand;
        std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink;
        perf_capture_configuration_t & configuration;
        perf_groups_activator_t counter_groups;
        cpu_info_t cpu_info {configuration};
        apc::misc_apc_frame_ipc_sender_t misc_apc_frame_ipc_sender {ipc_sink};
        std::shared_ptr<nl_kobject_uevent_cpu_monitor_t<>> nl_kobject_uevent_cpu_monitor {};
        std::shared_ptr<polling_cpu_monitor_t> polling_cpu_monitor {};
        std::set<pid_t> monitored_pids {};
        std::unique_ptr<sync_generator> sync_thread {};
        bool enable_on_exec {};

        /**
         * For a single cpu, read the initial counter values for any counters that must be polled on start up.
         *
         * Currently, this is only for the cpu_frequency counter.
         *
         * @param monotonic_start The capture start timestamp (in CLOCK_MONOTONIC_RAW)
         * @param cpu_no The number of the cpu for which counters should be polled
         */
        [[nodiscard]] auto co_read_initial_counter_value(std::uint64_t monotonic_start, cpu_no_t cpu_no)
        {
            using namespace async::continuations;

            auto st = shared_from_this();

            return start_on(strand) //
                 | then([st, monotonic_start, cpu_no]() -> polymorphic_continuation_t<> {
                       // read the counter
                       auto counter = read_cpu_frequency(cpu_no,
                                                         st->cpu_info,
                                                         st->configuration.cluster_keys_for_cpu_frequency_counter);

                       if (!counter) {
                           return {};
                       }

                       std::array<apc::perf_counter_t, 1> counter_values {{
                           *counter,
                       }};

                       return st->misc_apc_frame_ipc_sender.async_send_perf_counters_frame(
                                  monotonic_delta_now(monotonic_start),
                                  counter_values,
                                  use_continuation) //
                            | map_error();
                   });
        }

        /**
         * For all cpus, read the initial counter values for any counters that must be polled on start up.
         *
         * @param monotonic_start The capture start timestamp (in CLOCK_MONOTONIC_RAW)
         */
        [[nodiscard]] auto co_read_initial_counter_values(std::uint64_t monotonic_start)
        {
            using namespace async::continuations;

            return iterate(0UL, cpu_info.getNumberOfCores(), [st = shared_from_this(), monotonic_start](auto cpu_no) {
                return st->co_read_initial_counter_value(monotonic_start, cpu_no);
            });
        }

        /**
         * Poll all currently running processes/threads in /proc and write their basic properties (pid, tid, comm, exe) into the capture
         *
         * @param monotonic_start The capture start timestamp (in CLOCK_MONOTONIC_RAW)
         */
        [[nodiscard]] auto co_read_process_properties(std::uint64_t monotonic_start)
        {
            // TODO - SDDAP-11253
            using namespace async::continuations;

            return start_with();
        }

        /**
         * Poll all currently running processes/threads in /proc and write their `maps` file contents into the capture
         *
         * @param monotonic_start The capture start timestamp (in CLOCK_MONOTONIC_RAW)
         */
        [[nodiscard]] auto co_read_process_maps(std::uint64_t monotonic_start)
        {
            // TODO - SDDAP-11254
            using namespace async::continuations;

            return start_with();
        }

        /**
         * Read the kallsyms file and write into the capture
         */
        [[nodiscard]] auto co_read_kallsyms()
        {
            // TODO - dep SDDAP-11229
            using namespace async::continuations;

            return start_with();
        }

        /**
         * Send a core name apc frame
         *
         * @param cpu_no The cpu number of the core to send the message for
         */
        [[nodiscard]] auto co_send_core_name_msg(cpu_no_t cpu_no)
        {
            using namespace async::continuations;

            auto st = shared_from_this();

            return start_on(st->strand) //
                 | then([st, cpu_no]() -> polymorphic_continuation_t<> {
                       // Don't send information on a cpu we know nothing about
                       const int cpu_id = st->cpu_info.getCpuIds()[cpu_no];
                       if (cpu_id == -1) {
                           return {};
                       }

                       // we use cpuid lookup here for look up rather than clusters because it maybe a cluster
                       // that wasn't known at start up
                       auto it = st->configuration.cpuid_to_core_name.find(cpu_id);
                       if (it != st->configuration.cpuid_to_core_name.end()) {
                           return st->misc_apc_frame_ipc_sender.async_send_core_name(cpu_no,
                                                                                     cpu_id,
                                                                                     it->second,
                                                                                     use_continuation)
                                | map_error();
                       }

                       // create the core name string
                       lib::printf_str_t<32> buf {"Unknown (0x%.3x)", cpu_id};
                       return st->misc_apc_frame_ipc_sender.async_send_core_name(cpu_no,
                                                                                 cpu_id,
                                                                                 std::string(buf),
                                                                                 use_continuation)
                            | map_error();
                   });
        }

        /**
         * Send the initial summary frame
         *
         * @param monotonic_start The capture start timestamp (in CLOCK_MONOTONIC_RAW)
         */
        [[nodiscard]] async::continuations::polymorphic_continuation_t<> co_send_summary_frame(
            std::uint64_t monotonic_start)
        {
            using namespace async::continuations;

            auto st = shared_from_this();
            auto state = create_perf_driver_summary_state(configuration.perf_config, monotonic_start);

            if (!state) {
                return start_with(
                           boost::asio::error::make_error_code(boost::asio::error::basic_errors::operation_aborted))
                     | map_error();
            }

            return start_with()
                 // send the summary
                 | misc_apc_frame_ipc_sender.async_send_summary_message(std::move(*state),
                                                                        use_continuation) //
                 | map_error()
                 // send core names
                 | iterate(0UL, cpu_info.getNumberOfCores(), [st](cpu_no_t cpu_no) {
                       return st->co_send_core_name_msg(cpu_no);
                   });
        }

        /**
         * Activate all the perf events for a given core, and start observing them in the ring buffer,
         * but do not necessarily enable the events.
         *
         * Events will only be enabled if `start_counters` was previously called, or we are `enable_on_exec` and
         * `co_exec_child` was previously completed.
         *
         * @param cpu_no The core for which to enable events
         * @return The async result will be a bool indicating true for successful onlining, and false for core is offline.
         */
        [[nodiscard]] auto co_prepare_per_core_events(cpu_no_t cpu_no)
        {
            // TODO: create all PEAs and buffer groups and start observign for events
            // - SDDAP-11258, SDDAP-11259
            using namespace async::continuations;

            return start_with(true);
        }

        /**
         * Deactivate all the perf events for a given core and stop observing them.
         *
         * @param cpu_no The core for which to enable events
         */
        [[nodiscard]] auto co_remove_per_core_events(cpu_no_t cpu_no)
        {
            // TODO: create all PEAs and buffer groups and start observign for events
            // DO NOT enable the PEAs yet
            // - SDDAP-11261
            using namespace async::continuations;

            return start_with();
        }

        /**
         * Rescan for any changes to the CPU info, sending the appropriate core name message
         *
         * @param cpu_no The core for which to enable events
         */
        [[nodiscard]] auto co_rescan_cpu_info(cpu_no_t cpu_no)
        {
            using namespace async::continuations;

            return start_on(strand)                                                    //
                 | then([st = shared_from_this()]() { st->cpu_info.updateIds(true); }) //
                 | co_send_core_name_msg(cpu_no);
        }

        /**
         * Output any cpu online/offline event messages as part of a state change
         *
         * @param monotonic_start The capture start timestamp (in CLOCK_MONOTONIC_RAW)
         * @param cpu_no The core for which to enable events
         * @param online True if the core was online, false if it was offline
         */
        [[nodiscard]] auto co_core_state_change_msg(std::uint64_t monotonic_start, cpu_no_t cpu_no, bool online)
        {

            using namespace async::continuations;

            // TODO: Create the apc frame
            return ipc_sink->async_send_message(
                       ipc::msg_cpu_state_change_t {{monotonic_delta_now(monotonic_start), cpu_no, online}},
                       use_continuation)
                 | map_error_and_discard();
        }

        /**
         * Handle a state change event from the CPU online/offline monitor
         *
         * @param monotonic_start The capture start timestamp (in CLOCK_MONOTONIC_RAW)
         * @param cpu_no The core for which to enable events
         * @param online True if the core was online, false if it was offline
         */
        [[nodiscard]] auto co_update_cpu_state(std::uint64_t monotonic_start, cpu_no_t cpu_no, bool online)
        {
            using namespace async::continuations;

            auto st = shared_from_this();

            return start_on(strand) //
                 | predicate([st]() { return !st->is_terminated(); })
                 | do_if_else(
                       [online]() { return online; },
                       // when online
                       [st, monotonic_start, cpu_no]() {
                           return start_with()
                                //  rescan for the updated cpuid
                                | st->co_rescan_cpu_info(cpu_no)
                                // then create the PEAs and attach them to the buffer
                                | st->co_prepare_per_core_events(cpu_no)
                                // act according to whether or not the core actually was online (as it could go off again during activation)
                                | then([st, monotonic_start, cpu_no](
                                           bool really_online) mutable -> polymorphic_continuation_t<> {
                                      // if it didnt come online for some reason, then send an offline event
                                      if (!really_online) {
                                          return start_with()
                                               // deactivate all the events
                                               | st->co_remove_per_core_events(cpu_no)
                                               // write out an offline APC frame
                                               | st->co_core_state_change_msg(monotonic_start, cpu_no, false);
                                      }

                                      // is online, then read its counters and write out state change msg
                                      return start_with()
                                           // read the initial freq value
                                           | st->co_read_initial_counter_value(monotonic_start, cpu_no)
                                           // write out an online/offline APC frame
                                           | st->co_core_state_change_msg(monotonic_start, cpu_no, true);
                                  });
                       },
                       // when offline
                       [st, monotonic_start, cpu_no]() {
                           return start_with()
                                //  deactivate all the events
                                | st->co_remove_per_core_events(cpu_no)
                                // write out an offline APC frame
                                | st->co_core_state_change_msg(monotonic_start, cpu_no, false);
                       });
        }

        /**
         * Launch any android package and then poll for the process to start.
         * Once the process is detected as running, the list of tracked pids is updated.
         */
        [[nodiscard]] auto co_wait_for_process()
        {
            // TODO
            // -- 1. tell the shell to launch any android package (msg_exec_target_app_t)
            // -- 2. poll proc for the process to start (check for termination)
            using namespace async::continuations;

            return start_with();
        }

        /**
         * Fork (but not exec) the child process. The process is forked so that its pid is known
         * and events may be attached to it. The process is only exec'd once the capture is
         * ready to start.
         */
        [[nodiscard]] auto co_fork_process()
        {
            // TODO
            // -- 1. fork the child process but do not exec it

            using namespace async::continuations;

            return start_with();
        }

        /**
         * Prepare any process that should be profiled; at the end of this operation the list
         * of tracked pids will contains one or more values prepresenting the processes to
         * profile.
         * When making a system-wide capture (without --app/--pid etc), or for where the pids
         * are already specified (with --pid) then this operation is a nop.
         */
        [[nodiscard]] auto co_prepare_process()
        {
            using namespace async::continuations;

            auto st = shared_from_this();

            return start_with()
                 | do_if([st]() { return !st->configuration.wait_process->empty(); }, //
                         [st]() { return st->co_wait_for_process(); })
                 | do_if([st]() { return st->configuration.command.has_value(); }, //
                         [st]() { return st->co_fork_process(); });
        }

        /**
         * Exec the child process forked previously for --app
         */
        [[nodiscard]] auto co_exec_child()
        {
            // TODO
            using namespace async::continuations;

            // - make sure to call start_counters, even if enable_on_exec is true,
            //   but only once the process has exec'd so that we don't somehow race
            //   a core coming online and the process exec-ing.

            return start_with();
        }

        /** @return True if the capture is terminated, false if not */
        [[nodiscard]] bool is_terminated() const
        {
            // TODO
            return false;
        }

        /** Terminate the running capture */
        void terminate()
        {
            // TODO

            if (sync_thread) {
                sync_thread->terminate();
            }
        };

        /** Enable all counters so that they start producing events */
        void start_counters() {
            // TODO
        };

        /**
         * Launch the SPE sync thread
         *
         * @param monotonic_start The capture start timestamp (in CLOCK_MONOTONIC_RAW)
         */
        void start_sync_thread(std::uint64_t monotonic_start)
        {
            if (!is_terminated()) {
                runtime_assert(sync_thread == nullptr, "start_sync_thread called twice");

                sync_thread = sync_generator::create(configuration.perf_config.has_attr_clockid_support,
                                                     counter_groups.hasSPE(),
                                                     ipc_sink);

                sync_thread->start(monotonic_start);
            }
        };

        /**
         * Common cpu monitoring setup code
         *
         * @tparam Monitor The monitor type
         * @param st The shared pointer to this
         * @param monitor The monitor pointer
         */
        template<typename Monitor>
        static void start_monitoring_cpus(std::uint64_t monotonic_start,
                                          std::shared_ptr<perf_capture_t> const & st,
                                          std::shared_ptr<Monitor> monitor)
        {
            using namespace async::continuations;

            // repeatedly consume online/offline events
            repeatedly(
                [st]() {
                    return start_on(st->strand) //
                         | then([st]() { return !st->is_terminated(); });
                }, //
                [st, monitor, monotonic_start]() mutable {
                    return monitor->async_receive_one(use_continuation) //
                         | map_error()                                  //
                         | then([st, monotonic_start](auto event) mutable {
                               return st->co_update_cpu_state(monotonic_start, event.cpu_no, event.online);
                           });
                })
                | finally([st](auto err) {
                      // log the failure
                      error_swallower_t::consume("cpu monitoring", err);

                      // make sure to terminate
                      st->terminate();
                  });
        }

        /**
         * Start observing for CPU online events from netlink
         */
        void start_monitoring_uevents(std::uint64_t monotonic_start)
        {
            using namespace async::continuations;

            auto st = shared_from_this();
            auto monitor = nl_kobject_uevent_cpu_monitor;

            // should already be created and open
            runtime_assert(monitor != nullptr, "monitor is nullptr");
            runtime_assert(monitor->is_open(), "monitor is not open");

            start_monitoring_cpus(monotonic_start, shared_from_this(), std::move(monitor));
        }

        /**
         * Start observing for CPU online events by polling sysfs
         */
        void start_polling_cpus(std::uint64_t monotonic_start)
        {
            using namespace async::continuations;

            // expected to create it on first use
            runtime_assert(polling_cpu_monitor == nullptr, "polling_cpu_monitor is not nullptr");

            auto monitor = polling_cpu_monitor = std::make_shared<polling_cpu_monitor_t>(strand.context());

            start_monitoring_cpus(monotonic_start, shared_from_this(), std::move(monitor));
        }

    public:
        /**
         * Construct a new perf capture object
         *
         * @param context The io context
         * @param ipc_sink The raw ipc channel sink
         * @param configuration The configuration message contents
         */
        perf_capture_t(boost::asio::io_context & context,
                       std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink,
                       perf_capture_configuration_t & configuration)
            : strand(context),
              ipc_sink(std::move(ipc_sink)),
              configuration(configuration),
              counter_groups {
                  {
                      configuration.perf_config,
                      configuration.clusters,
                      configuration.per_core_cluster_index,
                      configuration.session_data.exclude_kernel_events || configuration.perf_config.exclude_kernel,
                  },
                  configuration.perf_groups,
              },
              nl_kobject_uevent_cpu_monitor(std::make_shared<nl_kobject_uevent_cpu_monitor_t<>>(context)),
              monitored_pids(std::move(configuration.pids))
        {
            if ((!configuration.perf_config.is_system_wide) && (!configuration.perf_config.has_attr_clockid_support)) {
                LOG_DEBUG("Tracing gatord as well as target application as no clock_id support");
                monitored_pids.insert(getpid());
            }

            // allow self profiling
#if (defined(GATOR_SELF_PROFILE) && (GATOR_SELF_PROFILE != 0))
            const bool profileGator = true;
#else
            const bool profileGator =
                (monitored_pids.erase(0) != 0); // user can set --pid 0 to dynamically enable this feature
#endif
            if (profileGator) {
                // track child and parent process
                monitored_pids.insert(getpid());
                monitored_pids.insert(getppid());
            }

            // was !enableOnCommandExec but this causes us to miss the exec comm record associated with the
            // enable on exec doesn't work for cpu-wide events.
            // additionally, when profiling gator, must be turned off
            this->enable_on_exec = (configuration.enable_on_exec && !configuration.perf_config.is_system_wide
                                    && configuration.perf_config.has_attr_clockid_support
                                    && configuration.perf_config.has_attr_comm_exec && !profileGator);
        }

        /**
         * Called once at agent start *after* the capture configuration is received, prepares the agent
         * ready for capture.
         */
        auto co_prepare()
        {
            using namespace async::continuations;

            auto st = shared_from_this();

            return start_on(strand)
                 // poll for process to start or fork (but not exec the app we are launching)
                 | co_prepare_process()
                 // tell the shell gator that we are ready
                 | then([st]() {
                       // must be in a then as we need to access the monitored_pids variable
                       return start_on(st->strand) //
                            | st->ipc_sink->async_send_message(
                                ipc::msg_capture_ready_t {std::vector<pid_t>(st->monitored_pids.begin(), //
                                                                             st->monitored_pids.end())}, //
                                use_continuation);
                   })
                 //
                 | map_error_and_discard();
        }

        /**
         * Called once the 'msg_start_t' message is received
         *
         * @param monotonic_start The monotonic start time
         */
        auto co_on_received_start_message(std::uint64_t monotonic_start)
        {
            using namespace async::continuations;

            auto st = shared_from_this();

            return start_on(strand)
                 // send the summary frame
                 | co_send_summary_frame(monotonic_start)
                 // bring online the core monitoring
                 | then([st, monotonic_start]() {
                       // monitor for cpu state changes (do this early so we don't miss anything)
                       if (st->nl_kobject_uevent_cpu_monitor && st->nl_kobject_uevent_cpu_monitor->is_open()) {
                           st->start_monitoring_uevents(monotonic_start);
                       }
                       else {
                           st->start_polling_cpus(monotonic_start);
                       }
                   })
                 // attempt to bring all cores online (any that were already started by the monitor will be ignored)
                 | iterate(0U,
                           st->configuration.num_cpu_cores,
                           [st, monotonic_start](cpu_no_t cpu_no) {
                               return st->co_update_cpu_state(monotonic_start, cpu_no, true);
                           })
                 // start the counters and sync thread
                 | then([st, monotonic_start]() {
                       // start generating sync events
                       st->start_sync_thread(monotonic_start);
                       // Start events before reading proc to avoid race conditions
                       if (!st->enable_on_exec) {
                           st->start_counters();
                       }
                   })
                 // send any manually read initial counter values
                 | st->co_read_initial_counter_values(monotonic_start)
                 // and the process initial properties
                 | st->co_read_process_properties(monotonic_start)
                 // and the contents of each process 'maps' file
                 | st->co_read_process_maps(monotonic_start)
                 // and the contents of kallsyms file
                 | st->co_read_kallsyms()
                 // exec the process (if there is one)
                 | st->co_exec_child();
        }
    };
}

/*
 things not in perpare that need to be done on the shell side before sending the capture configuration message:

            // Reread cpuinfo since cores may have changed since startup
            mCpuInfo.updateIds(false);

// must be done in shell
                if (!ftraceDriver.readTracepointFormats(buffer, printb, b1)) {
        LOG_DEBUG("FtraceDriver::readTracepointFormats failed");
        return false;
    }

*/

#pragma GCC diagnostic pop
