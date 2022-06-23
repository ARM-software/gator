/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "agents/perf/events/event_binding_manager.hpp"
#include "agents/perf/events/perf_activator.hpp"
#include "agents/perf/events/types.hpp"
#include "lib/error_code_or.hpp"
#include "linux/proc/ProcessChildren.h"

#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include <boost/asio/error.hpp>
#include <boost/system/error_code.hpp>

#include <unistd.h>

namespace agents::perf {
    /**
     * Helper for managing events and pids
     *
     * @tparam EventBindingManager The event_binding_manager_t (or a mock, for unit tests)
     */
    template<typename EventBindingManager = event_binding_manager_t<perf_activator_t>>
    class perf_capture_events_helper_t {
    public:
        using event_binding_manager_t = EventBindingManager;

        using event_binding_manager_type = std::decay_t<event_binding_manager_t>;
        using id_to_key_mappings_t = typename event_binding_manager_type::id_to_key_mappings_t;
        using stream_descriptor_t = typename event_binding_manager_type::stream_descriptor_t;
        using core_no_fd_pair_t = typename event_binding_manager_type::core_no_fd_pair_t;
        using fd_aux_flag_pair_t = typename event_binding_manager_type::fd_aux_flag_pair_t;

        static constexpr pid_t header_pid = event_binding_manager_type::header_pid;

        /** Returned by prepare_all_pid_trackers */
        struct prepare_all_pids_result_t {
            /** The set of monitored application tids */
            std::set<pid_t> monitored_tids;
            /** The mapping from event id to key */
            id_to_key_mappings_t id_to_key_mappings;
            /** The stream descriptors to monitor */
            std::vector<core_no_fd_pair_t> event_fds;
            /** The stream descriptors to monitor (but that don't count towards the traced process total) */
            std::vector<core_no_fd_pair_t> supplimentary_event_fds;
            /** The set of pid-resumers for paused pids, which must be preserved until after the events are started */
            std::map<pid_t, lnx::sig_continuer_t> paused_pids;
        };

        /** Returned by core_online_prepare */
        struct core_online_prepare_result_t {
            /** The mapping from event id to key */
            id_to_key_mappings_t mappings;
            /** The stream descriptors to monitor */
            std::vector<fd_aux_flag_pair_t> event_fds;
            /** The stream descriptors to monitor (but that don't count towards the traced process total) */
            std::vector<fd_aux_flag_pair_t> supplimentary_event_fds;
            /** The mmap */
            std::shared_ptr<perf_ringbuffer_mmap_t> mmap_ptr;
            /** The set of pid-resumers for paused pids, which must be preserved until after the events are started */
            std::map<pid_t, lnx::sig_continuer_t> paused_pids;
        };

        /** Constructor */
        perf_capture_events_helper_t(std::shared_ptr<perf_capture_configuration_t> const & configuration,
                                     event_binding_manager_t && event_binding_manager,
                                     std::set<pid_t> && monitored_pids)
            : event_binding_manager(std::forward<event_binding_manager_t>(event_binding_manager)),
              monitored_pids(std::move(monitored_pids)),
              is_system_wide(configuration->perf_config.is_system_wide),
              stop_on_exit(configuration->session_data.stop_on_exit || !configuration->perf_config.is_system_wide),
#if (defined(GATOR_SELF_PROFILE) && GATOR_SELF_PROFILE)
              profile_gator(true),
#else
              // allow self profiling
              // user can set --pid 0 to dynamically enable this feature
              profile_gator(remove_pid_zero()),
#endif
              // older kernels require monitoring of the sync-thread
              requires_process_events_from_self((!configuration->perf_config.is_system_wide)
                                                && (!configuration->perf_config.has_attr_clockid_support)),
              // was perf_config.enable_on_exec but this causes us to miss the exec comm record associated with the initial command, plus
              // enable on exec doesn't work for cpu-wide events.
              // additionally, when profiling gator, must be turned off
              enable_on_exec(configuration->enable_on_exec && !configuration->perf_config.is_system_wide
                             && configuration->perf_config.has_attr_clockid_support
                             && configuration->perf_config.has_attr_comm_exec && !profile_gator),
              // should we pause pids using SIGSTOP when preparing pids or bringing a core online
              stop_pids(configuration->stop_pids)
        {
#if (defined(GATOR_SELF_PROFILE) && GATOR_SELF_PROFILE)
            (void) remove_pid_zero();
#endif

            if (requires_process_events_from_self) {
                LOG_DEBUG("Tracing gatord as well as target application as no clock_id support");
            }
            if (profile_gator) {
                LOG_DEBUG("Tracing gatord as well as self-profiling requested");
            }
        }

        /** @return True if self-profiling was requested, false otherwise */
        [[nodiscard]] bool is_profile_gator() const { return profile_gator; }

        /** @return True if the perf agent must also be profiled (as the older kernel does not support clock id configuration) */
        [[nodiscard]] bool is_requires_process_events_from_self() const { return requires_process_events_from_self; }

        /** @return True if the captured events are enable-on-exec, rather than started manually */
        [[nodiscard]] bool is_enable_on_exec() const { return enable_on_exec; }

        /** @return True if configured counter groups include the SPE group */
        [[nodiscard]] bool has_spe() const { return event_binding_manager.has_spe(); }

        /** @return True if stop on exit is set */
        [[nodiscard]] bool is_stop_on_exit() const { return stop_on_exit; }

        /** @return The set of monitored pids */
        [[nodiscard]] std::set<pid_t> const & get_monitored_pids() const { return monitored_pids; }

        /** @return The set of monitored pids */
        [[nodiscard]] std::set<pid_t> const & get_monitored_gatord_pids() const { return monitored_gatord_tids; }

        /** Add a pid to the list to be monitored */
        void add_monitored_pid(pid_t pid) { monitored_pids.insert(pid); }

        /** Mark the capture as having started */
        void set_capture_started() { event_binding_manager.set_capture_started(); }

        /**
         * Add a set of tids to the set of monitored pids, but send SIGSTOP to them if required to.
         *
         * If the tids are stopped, they are held in a paused state until `clear_stopped_tids` is called.
         *
         * @param pids The set of pids to add
         */
        void add_stoppable_pids(std::set<pid_t> const & pids)
        {
            if (stop_pids && !is_system_wide) {
                // get the perf agent pids
                auto [just_agent_tids, all_gatord_tids] = find_gatord_tids();
                (void) just_agent_tids; //gcc7 :-(

                // SIGSTOP all pids so that they wait
                auto actual_tids = lnx::stop_all_tids(pids, all_gatord_tids, all_stopped_tids);

                // add the detected tids to the monitor
                for (auto tid : actual_tids) {
                    add_monitored_pid(tid);
                }
            }
            else {
                // add the detected pids to the monitor
                for (auto pid : pids) {
                    add_monitored_pid(pid);
                }
            }
        }

        /** Clear the set of stopped tids, which will cause them to resume */
        void clear_stopped_tids()
        {
            initial_pause_complete = true;
            all_stopped_tids.clear();
        }

        /**
         * Remove the --app pid
         *
         * @param pid The forked pid
         * @return true If all monitored pids are removed and stop_on_exec is set, otherwise false
         */
        [[nodiscard]] bool remove_command_pid(pid_t pid)
        {
            monitored_pids.erase(pid);

            return monitored_pids.empty() && stop_on_exit;
        }

        /**
         * Prepare all the monitored pids, their child threads are detected and added to the event monitor
         *
         * @param is_terminate_requested  A callable `bool()` that returns true if the capture is terminated asynchronously, false otherwise
         * @return nullopt is returned if the capture should terminate (due to request or error), otherwise the list of id->key mappings and set of actually monitored tids is returned
         */
        template<typename Callback>
        [[nodiscard]] std::optional<prepare_all_pids_result_t> prepare_all_pid_trackers(
            Callback && is_terminate_requested)
        {
            std::set<pid_t> actually_monitored_tids {};
            std::set<pid_t> actually_monitored_gatord_tids {};
            id_to_key_mappings_t all_id_key_mappings {};
            std::map<pid_t, lnx::sig_continuer_t> paused_pids {};
            std::vector<core_no_fd_pair_t> event_fds {};
            std::vector<core_no_fd_pair_t> supplimentary_event_fds {};

            // collect the monitored pids and their tids
            auto monitored_tids = find_monitored_tids();

            // get the perf agent pids
            auto [just_agent_tids, all_gatord_tids] = find_gatord_tids();

            // dont actually do anything other than check for exit in s-w mode
            if (!is_system_wide) {
                // pause any tids to avoid racing thread creation? ?
                if (stop_pids || initial_pause_complete) {
                    monitored_tids = filter_and_pause_tids(all_gatord_tids, monitored_tids, paused_pids);
                }

                if (!prepare_app_tids(monitored_tids,
                                      all_gatord_tids,
                                      just_agent_tids,
                                      actually_monitored_tids,
                                      actually_monitored_gatord_tids,
                                      all_id_key_mappings,
                                      event_fds,
                                      supplimentary_event_fds,
                                      is_terminate_requested)) {
                    return std::nullopt;
                }
            }
            else {
                // remove any tids in all_gatord_tids from monitored_tids as for the stop-on-exit check
                for (pid_t tid : monitored_tids) {
                    if (all_gatord_tids.count(tid) > 0) {
                        // remove it from monitored_*pids* as it should not count towards the all_requested_tids_exited check
                        monitored_pids.erase(tid);
                        continue;
                    }

                    actually_monitored_tids.insert(tid);
                }
            }

            // stop now if terminated
            if (is_terminate_requested()) {
                return std::nullopt;
            }

            // have all the requested pids exited?
            auto const all_requested_tids_exited =
                (actually_monitored_tids.empty() && ((!monitored_pids.empty()) || (!is_system_wide)));

            // replace the requested set with the actual set as it will be used later by the start_capture method
            monitored_pids = actually_monitored_tids;
            monitored_gatord_tids = std::move(actually_monitored_gatord_tids);

            // terminate if some pids were requested but none were actually monitored
            if (stop_on_exit && all_requested_tids_exited) {
                LOG_DEBUG("Terminating as no pids were monitorable");
                return std::nullopt;
            }

            return prepare_all_pids_result_t {std::move(actually_monitored_tids),
                                              std::move(all_id_key_mappings),
                                              std::move(event_fds),
                                              std::move(supplimentary_event_fds),
                                              std::move(paused_pids)};
        }

        /**
         * Start all the tracked pid events
         *
         * @return true on success, otherwise false if the capture should be terminated
         */
        [[nodiscard]] bool start_all_pid_trackers()
        {
            // nothing to do ?
            if (monitored_pids.empty() || is_system_wide) {
                return true;
            }

            // start each pid
            std::size_t n_started = 0;
            for (auto it = monitored_pids.begin(); it != monitored_pids.end();) {
                auto pid = *it;
                auto result = event_binding_manager.pid_track_start(pid);
                switch (result.state) {
                    case aggregate_state_t::failed: {
                        LOG_ERROR("Could not profile pid=%d due to unexpected error", pid);
                        return false;
                    }
                    case aggregate_state_t::terminated: {
                        LOG_ERROR("Could not profile pid=%d as it has terminated", pid);
                        // erase item
                        it = monitored_pids.erase(it);
                        break;
                    }
                    case aggregate_state_t::offline:
                    case aggregate_state_t::usable: {
                        // these are fine
                        n_started += 1;
                        // move to next item
                        ++it;
                        break;
                    }
                    default: {
                        throw std::runtime_error("unexpected case aggregate_state_t");
                    }
                }
            }

            // returning false indicates capture termination
            return ((n_started > 0) || (!stop_on_exit));
        }

        /**
         * Prepare any events when a cpu core comes online
         *
         * @param core_no The core no to online
         * @param cluster_id The cluster id associated with that core
         * @return An error code is returned on failure, or success if the core went offline but no error occured, otherwise the event binding manager result is returned for successful online event
         */
        [[nodiscard]] lib::error_code_or_t<core_online_prepare_result_t> core_online_prepare(
            core_no_t core_no,
            cpu_cluster_id_t cluster_id)
        {
            std::set<pid_t> additional_tids {};
            std::set<pid_t> supplimentary_tids {};
            std::map<pid_t, lnx::sig_continuer_t> paused_pids {};
            std::vector<fd_aux_flag_pair_t> event_fds {};
            std::vector<fd_aux_flag_pair_t> supplimentary_event_fds {};

            // Scan for any new tids; these will be added to the EBMs set of known tids and activated for any core that subsequently comes online (including this one)
            // but not for any cores that are already online as it is assumed the tid will be tracked via the 'inherit' bit
            if (!is_system_wide) {
                // collect the monitored pids and their tids
                auto monitored_tids = find_monitored_tids();

                // get the perf agent pids
                auto [just_agent_tids, all_gatord_tids] = find_gatord_tids();
                (void) just_agent_tids; // gcc 7 :-()

                // pause any tids to avoid racing thread creation? ?
                if (stop_pids || initial_pause_complete) {
                    monitored_tids = filter_and_pause_tids(all_gatord_tids, monitored_tids, paused_pids);
                }

                // collect the set of tids that are new
                for (pid_t tid : monitored_tids) {
                    if (all_gatord_tids.count(tid) == 0) {
                        // new tid detected, save it for passing to core_online_prepare
                        additional_tids.insert(tid);
                        // and add to the set of tracked pids
                        if (monitored_pids.insert(tid).second) {
                            LOG_DEBUG("core_online_prepare detected new tid %d", tid);
                        }
                    }
                }

                supplimentary_tids = std::move(all_gatord_tids);
            }

            auto result = event_binding_manager.core_online_prepare(core_no, cluster_id, additional_tids);

            switch (result.state) {
                case aggregate_state_t::failed: {
                    return {boost::system::error_code {boost::asio::error::bad_descriptor}};
                }
                case aggregate_state_t::offline:
                case aggregate_state_t::terminated: {
                    if (remove_terminated(result.terminated_pids) && stop_on_exit) {
                        return {boost::system::error_code {boost::asio::error::eof}};
                    }
                    return {boost::system::error_code {}};
                }
                case aggregate_state_t::usable: {
                    if (remove_terminated(result.terminated_pids) && stop_on_exit) {
                        return {boost::system::error_code {boost::asio::error::eof}};
                    }

                    for (auto entry : result.event_fds_by_pid) {
                        if ((entry.first == header_pid) || (supplimentary_tids.count(entry.first) > 0)) {
                            supplimentary_event_fds.emplace_back(entry.second);
                        }
                        else {
                            event_fds.emplace_back(entry.second);
                        }
                    }

                    return core_online_prepare_result_t {std::move(result.mappings),
                                                         std::move(event_fds),
                                                         std::move(supplimentary_event_fds),
                                                         std::move(result.mmap_ptr),
                                                         std::move(paused_pids)};
                }
                default: {
                    throw std::runtime_error("what aggregate_state_t is this?");
                }
            };
        }

        /**
         * Start the core after preparing it
         *
         * @param core_no The core to online
         * @return A pair, being an error code, and a bool flag indicating online/offline state
         */
        [[nodiscard]] std::pair<boost::system::error_code, bool> core_online_start(core_no_t core_no)
        {
            // just finish, if the capture has not started
            if (!event_binding_manager.is_capture_started()) {
                return {boost::system::error_code {}, true};
            }

            // otherwise start the events
            auto result = event_binding_manager.core_online_start(core_no);

            switch (result.state) {
                case aggregate_state_t::failed: {
                    return {boost::system::error_code {boost::asio::error::bad_descriptor}, false};
                }
                case aggregate_state_t::offline:
                case aggregate_state_t::terminated: {
                    if (remove_terminated(result.terminated_pids) && stop_on_exit) {
                        return {boost::system::error_code {boost::asio::error::eof}, false};
                    }
                    return {boost::system::error_code {}, false};
                }
                case aggregate_state_t::usable: {
                    if (remove_terminated(result.terminated_pids) && stop_on_exit) {
                        return {boost::system::error_code {boost::asio::error::eof}, false};
                    }
                    return {boost::system::error_code {}, true};
                }
                default: {
                    throw std::runtime_error("what aggregate_state_t is this?");
                }
            };
        }

        /**
         * Close events associated with some core as the core went offline
         *
         * @param core_no The core that went offline
         */
        void core_offline(core_no_t core_no) { event_binding_manager.core_offline(core_no); }

    private:
        event_binding_manager_t event_binding_manager;
        std::set<pid_t> monitored_pids;
        std::set<pid_t> monitored_gatord_tids {};
        std::map<pid_t, lnx::sig_continuer_t> all_stopped_tids {};
        bool const is_system_wide;
        bool const stop_on_exit;
        bool const profile_gator;
        bool const requires_process_events_from_self;
        bool const enable_on_exec;
        bool const stop_pids;
        bool initial_pause_complete {false};

        /**
         * Remove any monitored pids from the set, that are indicated as terminated by the event binding manager
         *
         * @param terminated_pids The pids indicated as terminated
         * @return true if the set of monitored events is empty, false otherwise
         */
        [[nodiscard]] bool remove_terminated(std::set<pid_t> const & terminated_pids)
        {
            // if no pids were terminated, don't check the monitored set, as it may be
            // empty anyway in system-wide mode
            if (terminated_pids.empty()) {
                return false;
            }

            for (auto pid : terminated_pids) {
                monitored_pids.erase(pid);
            }

            return monitored_pids.empty();
        }

        /** Remove pid zero from the set of monitored pids as it has special meaning.
         * @return true if the pid was removed, false if the set did not contain it
         */
        [[nodiscard]] bool remove_pid_zero() { return (monitored_pids.erase(0) != 0); }

        /** Prepare one pid with the event binding manager */
        [[nodiscard]] bool pid_track_prepare(pid_t tid,
                                             std::set<pid_t> & actually_monitored_tids,
                                             id_to_key_mappings_t & all_id_key_mappings,
                                             std::vector<core_no_fd_pair_t> & event_fds)
        {
            LOG_DEBUG("Attaching to pid %d", tid);

            // track another tid
            auto result = event_binding_manager.pid_track_prepare(tid);
            switch (result.state) {
                case aggregate_state_t::failed: {
                    LOG_ERROR("Could not profile tid=%d due to unexpected error", tid);
                    return false;
                }
                case aggregate_state_t::terminated: {
                    LOG_ERROR("Could not profile tid=%d as it has terminated", tid);
                    return true;
                }
                case aggregate_state_t::offline: {
                    // nothing to do, the cpu was currently offline
                    actually_monitored_tids.insert(tid);
                    return true;
                }
                case aggregate_state_t::usable: {
                    // add the id->key mappings to the set for sending to the shell
                    all_id_key_mappings.insert(all_id_key_mappings.end(),
                                               result.mappings.begin(),
                                               result.mappings.end());
                    // record the fact that it was successful
                    actually_monitored_tids.insert(tid);
                    // update event_fds_by_pid
                    event_fds.insert(event_fds.end(),
                                     result.event_fds_by_core_no.begin(),
                                     result.event_fds_by_core_no.end());
                    return true;
                }
                default: {
                    throw std::runtime_error("unexpected case aggregate_state_t");
                }
            }
        }

        /** Prepare the various tids for app-profiling mode */
        template<typename Callback>
        [[nodiscard]] bool prepare_app_tids(
            std::set<pid_t> const & monitored_tids, // NOLINT(bugprone-easily-swappable-parameters)
            std::set<pid_t> const & all_gatord_tids,
            std::set<pid_t> const & just_agent_tids,
            std::set<pid_t> & actually_monitored_tids,
            std::set<pid_t> & actually_monitored_gatord_tids,
            id_to_key_mappings_t & all_id_key_mappings,
            std::vector<core_no_fd_pair_t> & event_fds,
            std::vector<core_no_fd_pair_t> & supplimentary_event_fds,
            Callback && is_terminate_requested)
        {
            // prepare all the pids
            for (pid_t tid : monitored_tids) {
                // stop now if terminated
                if (is_terminate_requested()) {
                    return false;
                }

                // remove any tids in all_gatord_tids from monitored_tids as they are to be handled separately
                if (all_gatord_tids.count(tid) > 0) {
                    LOG_DEBUG("Ignoring gatord pid %d", tid);
                    // remove it from monitored_*pids* as it should not count towards the all_requested_tids_exited check
                    monitored_pids.erase(tid);
                    continue;
                }

                if (!pid_track_prepare(tid, actually_monitored_tids, all_id_key_mappings, event_fds)) {
                    return false;
                }
            }

            // if profile-self is requested then add everything from all_gatord_tids
            if (profile_gator) {
                for (pid_t tid : all_gatord_tids) {

                    // stop now if terminated
                    if (is_terminate_requested()) {
                        return false;
                    }

                    if (!pid_track_prepare(tid,
                                           actually_monitored_gatord_tids,
                                           all_id_key_mappings,
                                           supplimentary_event_fds)) {
                        // it is only fatal if the pid came from the perf agent and requires_process_events_from_self is true
                        if (requires_process_events_from_self && (just_agent_tids.count(tid) > 0)) {
                            return false;
                        }
                    }
                }
            }
            //  otherwise, if just self is required, then add just_agent_tids
            else if (requires_process_events_from_self) {
                for (pid_t tid : just_agent_tids) {
                    // stop now if terminated
                    if (is_terminate_requested()) {
                        return false;
                    }

                    if (!pid_track_prepare(tid,
                                           actually_monitored_gatord_tids,
                                           all_id_key_mappings,
                                           supplimentary_event_fds)) {
                        return false;
                    }
                }
            }

            return true;
        }

        /**
         * Sends SIGSTOP to all the monitored tids (that are not gatord tids), then updates the list of monitored tids
         * to reflect any additionally detected tids. The set of paused tids is stored for later resumption
         *
         * @param all_gatord_tids The set of all gatord tids (which must not be stopped)
         * @param monitored_tids The set of app tids (which must be stopped)
         * @param paused_pids The map from pid to SIGCONT resumer for all stopped/detected tids
         * @return The set of tids that are actually to be monitored
         */
        [[nodiscard]] std::set<pid_t> filter_and_pause_tids(std::set<pid_t> const & all_gatord_tids,
                                                            std::set<pid_t> const & monitored_tids,
                                                            std::map<pid_t, lnx::sig_continuer_t> & paused_pids)
        {
            // pause all the pids in monitored_tids that are not in all_gatord_tids
            return lnx::stop_all_tids(
                monitored_tids,
                all_gatord_tids,
                // if the global paused set is still not resumed, then extend that, otherwise just temporarily pause them
                (initial_pause_complete ? paused_pids : all_stopped_tids));
        }

        /** collect the monitored pids and their tids */
        [[nodiscard]] std::set<pid_t> find_monitored_tids()
        {
            std::set<pid_t> result {};
            for (pid_t pid : monitored_pids) {
                lnx::addTidsRecursively(result, pid, true);
            }
            return result;
        }

        /** Collect the set of pids that belong to this agent and the gatord parent process */
        [[nodiscard]] std::pair<std::set<pid_t>, std::set<pid_t>> find_gatord_tids()
        {
            // get the perf agent pids
            std::set<pid_t> just_agent_tids = lnx::getChildTids(lib::getpid(), false);
            // then copy it and repeat recursively for the parent (gatord-child) pids, which will ignore any children of the perf agent
            // producing a set containing all gatord-child and agent threads, but not any forked command pid
            std::set<pid_t> all_gatord_tids = just_agent_tids;
            lnx::addTidsRecursively(all_gatord_tids, lib::getppid(), true);

            return {std::move(just_agent_tids), std::move(all_gatord_tids)};
        }
    };
}
