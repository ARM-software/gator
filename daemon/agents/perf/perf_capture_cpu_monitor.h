/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "Time.h"
#include "agents/common/coalescing_cpu_monitor.h"
#include "agents/common/nl_cpu_monitor.h"
#include "agents/common/polling_cpu_monitor.h"
#include "agents/perf/perf_capture_helper.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/continuation.h"
#include "async/continuations/operations.h"
#include "async/continuations/stored_continuation.h"
#include "async/continuations/use_continuation.h"

#include <memory>
#include <set>

#include <boost/asio/io_context.hpp>
#include <boost/asio/io_context_strand.hpp>

namespace agents::perf {
    /**
     * Monitors each CPU for online/offline and activates cores.
     * Templated base class (so that compile time substitution for unit testing is possible, use perf_capture_cpu_monitor_t instead)
     */
    template<typename PerfCaptureHelper, typename NetlinkCpuMonitor, typename PollingCpuMonitor>
    class basic_perf_capture_cpu_monitor_t
        : public std::enable_shared_from_this<
              basic_perf_capture_cpu_monitor_t<PerfCaptureHelper, NetlinkCpuMonitor, PollingCpuMonitor>> {
    private:
        using perf_capture_helper_t = PerfCaptureHelper;
        using nl_kobject_uevent_cpu_monitor_t = NetlinkCpuMonitor;
        using polling_cpu_monitor_t = PollingCpuMonitor;
        using all_cores_ready_handler_t = async::continuations::stored_continuation_t<bool>;

        boost::asio::io_context::strand strand;
        std::shared_ptr<perf_capture_helper_t> perf_capture_helper {};
        std::shared_ptr<coalescing_cpu_monitor_t> coalescing_cpu_monitor {};
        std::shared_ptr<nl_kobject_uevent_cpu_monitor_t> nl_kobject_uevent_cpu_monitor {};
        std::shared_ptr<polling_cpu_monitor_t> polling_cpu_monitor {};
        std::set<int> cores_having_received_initial_event {};
        all_cores_ready_handler_t all_cores_ready_handler {};
        std::size_t num_cpu_cores;
        bool terminated {false};
        bool notified_all_cores_ready_handler {false};

        /**
         * Perform the steps required to offline a cpu
         *
         * @param monotonic_start The capture start timestamp (in CLOCK_MONOTONIC_RAW)
         * @param cpu_no The core for which to enable events
         */
        [[nodiscard]] auto co_offline_cpu(std::uint64_t monotonic_start, int cpu_no)
        {
            using namespace async::continuations;

            LOG_DEBUG("Offlining cpu # %d", cpu_no);

            return
                //  deactivate all the events
                perf_capture_helper->async_remove_per_core_events(cpu_no, use_continuation)
                // write out an offline APC frame
                | perf_capture_helper->async_core_state_change_msg(monotonic_start, cpu_no, false, use_continuation);
        }

        /**
         * Perform the steps required to online a cpu
         *
         * @param monotonic_start The capture start timestamp (in CLOCK_MONOTONIC_RAW)
         * @param cpu_no The core for which to enable events
         */
        [[nodiscard]] auto co_online_cpu(std::uint64_t monotonic_start, int cpu_no)
        {
            using namespace async::continuations;

            LOG_DEBUG("Onlining cpu # %d", cpu_no);

            return
                //  rescan for the updated cpuid
                perf_capture_helper->async_rescan_cpu_info(cpu_no, use_continuation)
                // then create the PEAs and attach them to the buffer
                | perf_capture_helper->async_prepare_per_core_events(cpu_no, use_continuation)
                // act according to whether or not the core actually was online (as it could go off again during activation)
                | then([st = this->shared_from_this(), monotonic_start, cpu_no](
                           bool really_online) mutable -> polymorphic_continuation_t<> {
                      // if it didnt come online for some reason, then send an offline event
                      if (!really_online) {
                          LOG_DEBUG("Onlining cpu # %d failed as not all cores came online", cpu_no);
                          return st->co_offline_cpu(monotonic_start, cpu_no);
                      }

                      // is online, then read its counters and write out state change msg
                      return
                          // read the initial freq value
                          st->perf_capture_helper->async_read_initial_counter_value(
                              monotonic_start,
                              cpu_no,
                              async::continuations::use_continuation)
                          // write out an online/offline APC frame
                          | st->perf_capture_helper->async_core_state_change_msg(monotonic_start,
                                                                                 cpu_no,
                                                                                 true,
                                                                                 use_continuation);
                  });
        }

        /**
         * Handle a state change event from the CPU online/offline monitor
         *
         * @param monotonic_start The capture start timestamp (in CLOCK_MONOTONIC_RAW)
         * @param cpu_no The core for which to enable events
         * @param online True if the core was online, false if it was offline
         * @param token The completion token for the async operation
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_update_cpu_state(std::uint64_t monotonic_start,
                                                  int cpu_no,
                                                  bool online,
                                                  CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate(
                [st = this->shared_from_this(), monotonic_start, cpu_no, online]() {
                    return start_on(st->strand) //
                         | do_if([st, cpu_no]() { return (cpu_no >= 0) && (!st->is_terminated()); },
                                 [st, monotonic_start, cpu_no, online]() {
                                     return start_with() //
                                          | do_if_else([online]() { return online; },
                                                       // when online
                                                       [st, monotonic_start, cpu_no]() {
                                                           return st->co_online_cpu(monotonic_start, cpu_no);
                                                       },
                                                       // when offline
                                                       [st, monotonic_start, cpu_no]() {
                                                           return st->co_offline_cpu(monotonic_start, cpu_no);
                                                       });
                                 });
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Common cpu monitoring setup code
         *
         * @tparam Monitor The monitor type
         * @param st The shared pointer to this
         * @param monitor The monitor pointer
         */
        template<typename Monitor>
        static void start_monitoring_cpus(std::uint64_t monotonic_start,
                                          std::shared_ptr<basic_perf_capture_cpu_monitor_t> st,
                                          std::shared_ptr<Monitor> monitor)
        {
            using namespace async::continuations;

            auto coalescing_cpu_monitor = st->coalescing_cpu_monitor;

            // repeatedly consume online/offline events from the underlying monitor and inject them into the coalescing monitor
            spawn("cpu monitoring (from raw)",
                  repeatedly(
                      [st]() {
                          return start_on(st->strand) //
                               | then([st]() { return !st->is_terminated(); });
                      }, //
                      [coalescing_cpu_monitor, monitor]() mutable {
                          return monitor->async_receive_one(use_continuation) //
                               | map_error()                                  //
                               | then([coalescing_cpu_monitor](auto event) mutable {
                                     return coalescing_cpu_monitor->async_update_state(event.cpu_no,
                                                                                       event.online,
                                                                                       use_continuation);
                                 });
                      }),
                  [st](bool) {
                      // make sure to terminate
                      st->terminate();
                  });

            // repeatedly consume online/offline events from the coalescing monitor
            spawn("cpu monitoring (from coalescer)",
                  repeatedly(
                      [st]() {
                          return start_on(st->strand) //
                               | then([st]() { return !st->is_terminated(); });
                      }, //
                      [st, coalescing_cpu_monitor, monotonic_start]() mutable {
                          return coalescing_cpu_monitor->async_receive_one(use_continuation) //
                               | map_error()                                                 //
                               | then([st, monotonic_start](auto event) mutable {
                                     return st->async_update_cpu_state(monotonic_start,
                                                                       event.cpu_no,
                                                                       event.online,
                                                                       use_continuation) //
                                          | post_on(st->strand)                          //
                                          | then([st, cpu_no = event.cpu_no]() {
                                                st->check_cores_having_received_initial_event(cpu_no);
                                            });
                                 });
                      }),
                  [st](bool) {
                      // make sure to terminate
                      st->terminate();
                  });
        }

        /**
         * Check / notify the handler when all cores have received on event
         *
         * @param cpu_no The core that received an event
         */
        void check_cores_having_received_initial_event(int cpu_no)
        {
            if ((cpu_no < 0) || (std::size_t(cpu_no) > num_cpu_cores)) {
                return;
            }

            auto [it, inserted] = cores_having_received_initial_event.insert(cpu_no);
            (void) it; // gcc 7 :-(

            if (inserted) {
                LOG_DEBUG("Core %d received its first event", cpu_no);
            }

            if (inserted && (cores_having_received_initial_event.size() == num_cpu_cores)) {
                LOG_DEBUG("All cores are now ready");
                all_cores_ready_handler_t all_cores_ready_handler {std::move(this->all_cores_ready_handler)};
                if (all_cores_ready_handler) {
                    LOG_DEBUG("Notifiying that all are ready");
                    notified_all_cores_ready_handler = true;
                    resume_continuation(strand.context(), std::move(all_cores_ready_handler), !terminated);
                }
            }
        }

        /**
         * Start observing for CPU online events from netlink
         */
        void start_monitoring_uevents(std::uint64_t monotonic_start)
        {
            using namespace async::continuations;

            auto st = this->shared_from_this();
            auto monitor = nl_kobject_uevent_cpu_monitor;

            // should already be created and open
            runtime_assert(monitor != nullptr, "monitor is nullptr");
            runtime_assert(monitor->is_open(), "monitor is not open");

            start_monitoring_cpus(monotonic_start, std::move(st), std::move(monitor));
        }

        /**
         * Start observing for CPU online events by polling sysfs
         */
        void start_polling_cpus(std::uint64_t monotonic_start)
        {
            using namespace async::continuations;

            auto monitor = polling_cpu_monitor;

            // create it on demand if necessary
            if (monitor == nullptr) {
                polling_cpu_monitor = monitor = std::make_shared<polling_cpu_monitor_t>(strand.context());
            }

            start_monitoring_cpus(monotonic_start, this->shared_from_this(), std::move(monitor));
        }

    public:
        basic_perf_capture_cpu_monitor_t(boost::asio::io_context & context,
                                         std::size_t num_cpu_cores,
                                         std::shared_ptr<perf_capture_helper_t> perf_capture_helper)
            : strand(context),
              perf_capture_helper(std::move(perf_capture_helper)),
              coalescing_cpu_monitor(std::make_shared<coalescing_cpu_monitor_t>(context)),
              nl_kobject_uevent_cpu_monitor(std::make_shared<nl_kobject_uevent_cpu_monitor_t>(context)),
              polling_cpu_monitor(),
              num_cpu_cores(num_cpu_cores)
        {
        }

        basic_perf_capture_cpu_monitor_t(boost::asio::io_context & context,
                                         std::size_t num_cpu_cores,
                                         std::shared_ptr<perf_capture_helper_t> perf_capture_helper,
                                         std::shared_ptr<nl_kobject_uevent_cpu_monitor_t> nl_kobject_uevent_cpu_monitor,
                                         std::shared_ptr<polling_cpu_monitor_t> polling_cpu_monitor)
            : strand(context),
              perf_capture_helper(std::move(perf_capture_helper)),
              coalescing_cpu_monitor(std::make_shared<coalescing_cpu_monitor_t>(context)),
              nl_kobject_uevent_cpu_monitor(std::move(nl_kobject_uevent_cpu_monitor)),
              polling_cpu_monitor(std::move(polling_cpu_monitor)),
              num_cpu_cores(num_cpu_cores)
        {
        }

        /** @return True if the capture is terminated, false if not */
        [[nodiscard]] bool is_terminated() const { return terminated; }

        /** Terminate the running capture */
        void terminate()
        {
            if (terminated) {
                return;
            }

            LOG_DEBUG("Terminating Perf CPU monitor");

            terminated = true;

            auto nl_kobject_uevent_cpu_monitor = this->nl_kobject_uevent_cpu_monitor;
            if (nl_kobject_uevent_cpu_monitor) {
                nl_kobject_uevent_cpu_monitor->stop();
            }

            auto polling_cpu_monitor = this->polling_cpu_monitor;
            if (polling_cpu_monitor) {
                polling_cpu_monitor->stop();
            }

            auto coalescing_cpu_monitor = this->coalescing_cpu_monitor;
            if (coalescing_cpu_monitor) {
                coalescing_cpu_monitor->stop();
            }

            auto perf_capture_helper = this->perf_capture_helper;
            if (perf_capture_helper) {
                perf_capture_helper->terminate();
            }

            // cancel any pending handler
            all_cores_ready_handler_t all_cores_ready_handler {std::move(this->all_cores_ready_handler)};
            if (all_cores_ready_handler) {
                resume_continuation(strand.context(), std::move(all_cores_ready_handler), false);
            }
        };

        /**
         * Handle a state change event from the CPU online/offline monitor
         *
         * @param monotonic_start The capture start timestamp (in CLOCK_MONOTONIC_RAW)
         * @param token The completion token for the async operation
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_start_monitoring(std::uint64_t monotonic_start, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate(
                [st = this->shared_from_this(), monotonic_start]() {
                    // monitor for cpu state changes (do this early so we don't miss anything)
                    return start_on(st->strand) //
                         // attempt to bring all cores online at startup by injecting an initial online event
                         | iterate(std::size_t {0},
                                   st->num_cpu_cores,
                                   [st](int cpu_no) {
                                       return st->coalescing_cpu_monitor->async_update_state(cpu_no,
                                                                                             true,
                                                                                             use_continuation);
                                   })
                         // start monitoring events which will bring online/offline as appropriate
                         | then([st, monotonic_start]() {
                               if (st->nl_kobject_uevent_cpu_monitor && st->nl_kobject_uevent_cpu_monitor->is_open()) {
                                   // free the polling object
                                   st->polling_cpu_monitor.reset();
                                   // and start monitoring
                                   st->start_monitoring_uevents(monotonic_start);
                               }
                               else {
                                   // free the netlink object
                                   st->nl_kobject_uevent_cpu_monitor.reset();
                                   // and start monitoring
                                   st->start_polling_cpus(monotonic_start);
                               }
                           });
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Wait for all the cores to receive their first online/offline event
         */
        template<typename CompletionToken>
        [[nodiscard]] auto async_wait_for_all_cores_ready(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(bool)>(
                [st = this->shared_from_this()](auto && sc) {
                    submit(start_on(st->strand) //
                               | then([st, sc = sc.move()]() mutable {
                                     // cancel any pending handler
                                     all_cores_ready_handler_t all_cores_ready_handler {
                                         std::move(st->all_cores_ready_handler)};
                                     if (all_cores_ready_handler) {
                                         LOG_DEBUG("Cancelling pending handler");
                                         resume_continuation(st->strand.context(),
                                                             std::move(all_cores_ready_handler),
                                                             false);
                                     }

                                     // cancel it as already previously notified or terminated?
                                     if (st->notified_all_cores_ready_handler || st->terminated) {
                                         LOG_DEBUG("Cancelling pending handler as already notified");
                                         resume_continuation(st->strand.context(), std::move(sc), false);
                                     }
                                     // directly post this new one?
                                     else if ((st->cores_having_received_initial_event.size() == st->num_cpu_cores)) {
                                         LOG_DEBUG("Notifiying that all are ready");
                                         st->notified_all_cores_ready_handler = true;
                                         resume_continuation(st->strand.context(), std::move(sc), !st->terminated);
                                     }
                                     // otherwise just store it
                                     else {
                                         LOG_DEBUG("Storing ready handler");
                                         st->all_cores_ready_handler = std::move(sc);
                                     }
                                 }),
                           sc.get_exceptionally());
                },
                std::forward<CompletionToken>(token));
        }
    };

    /** The cpu monitor type */
    using perf_capture_cpu_monitor_t = basic_perf_capture_cpu_monitor_t<perf_capture_helper_t<>,
                                                                        nl_kobject_uevent_cpu_monitor_t<>,
                                                                        polling_cpu_monitor_t>;
}
