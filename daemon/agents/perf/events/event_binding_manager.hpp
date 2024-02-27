/* Copyright (C) 2021-2024 by Arm Limited. All rights reserved. */
#pragma once

#include "Logging.h"
#include "agents/perf/capture_configuration.h"
#include "agents/perf/events/event_bindings.hpp"
#include "agents/perf/events/event_configuration.hpp"
#include "agents/perf/events/perf_activator.hpp"
#include "agents/perf/events/perf_ringbuffer_mmap.hpp"
#include "agents/perf/events/types.hpp"
#include "lib/Assert.h"
#include "lib/EnumUtils.h"
#include "linux/perf/PerfUtils.h"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include <k/perf_event.h>
#include <sched.h>

namespace agents::perf {

    /**
     * This class provides the means to manage per-core / per-thread counter groups for CPU (i.e PMU/software/tracepoint, not uncore) related events.
     *
     * The manager will respond to core online/offline events, along with pid track/untrack events and activate groups of events on a per (core+thread)
     * basis (as appropriate for app vs system-wide mode). It will handle cases where the core is reported as offline during activation, or likewise
     * where the thread terminates.
     *
     * online and track events are split into two calls; a 'xxx_prepare' method which prepares the events with appropriate calls to perf_event_open.
     * The set of opened items is returned as id->key mappings, allowing the caller to serialize them into the APC capture. This may then
     * be followed by a call to 'xxx_start' method which will activate the perf event group.
     */
    template<typename PerfActivator = perf_activator_t>
    class event_binding_manager_t {
    public:
        using perf_activator_t = PerfActivator;
        using stream_descriptor_t = typename perf_activator_t::stream_descriptor_t;
        using event_binding_set_type = event_binding_set_t<stream_descriptor_t>;
        using id_to_key_mappings_t = std::vector<std::pair<perf_event_id_t, gator_key_t>>;

        /* The tuple of fd and is-aux flag */
        using fd_aux_flag_pair_t = std::pair<std::shared_ptr<stream_descriptor_t>, bool>;
        /** The tuple of pid, fd and is_aux flag */
        using pid_fd_pair_t = std::pair<pid_t, fd_aux_flag_pair_t>;
        /** The tuple of core no, fd and is_aux flag */
        using core_no_fd_pair_t = std::pair<core_no_t, fd_aux_flag_pair_t>;

        /** Returned by core_online_prepare */
        struct core_online_prepare_result_t {
            /** Indicates the state of the core, where:
             * - aggregate_state_t::usable means the core was online and had events attached to it.
             * - aggregate_state_t::failed means some unexpected fatal error occured. The core will be reverted to an offline state.
             * - aggregate_state_t::terminated means there are no threads currently tracked. The core will be usable once a thread is tracked.
             * - aggregate_state_t::offline means the core went offline again and will be left in that state with no events attached to it
             */
            aggregate_state_t state;
            /** The mapping from event id to key */
            id_to_key_mappings_t mappings {};
            /** The set of pids that were previously tracked, but were removed as were detected as terminated during the prepare call */
            std::set<pid_t> terminated_pids {};
            /** The stream descriptors to monitor */
            std::vector<pid_fd_pair_t> event_fds_by_pid {};
            /** The mmap */
            std::shared_ptr<perf_ringbuffer_mmap_t> mmap_ptr {};
        };

        /** Returned by core_online_start */
        struct core_online_start_result_t {
            /** Indicates the state of the core, where:
             * - aggregate_state_t::usable means the core was started correctly.
             * - aggregate_state_t::failed means some unexpected fatal error occured. The core will be reverted to an offline state.
             * - aggregate_state_t::terminated means there are no threads currently tracked. The core will be usable once a thread is tracked.
             * - aggregate_state_t::offline means the core went offline again before the call, or no prior call to core_online_prepare was made.
             */
            aggregate_state_t state;
            /** The set of pids that were previously tracked, but were removed as were detected as terminated during the start call */
            std::set<pid_t> terminated_pids {};
        };

        /** Returned by pid_track_prepare */
        struct pid_track_prepare_result_t {
            /** Indicates the state of the pid, where:
             * - aggregate_state_t::usable means the pid was tracked and attached to at least one online core.
             * - aggregate_state_t::failed means some unexpected fatal error occured. The pid will be reverted to an untracked state.
             * - aggregate_state_t::terminated means that the pid terminated during the prepare call and is reverted to an untracked state.
             * - aggregate_state_t::offline means no cores are currently online, or all that previously were online went offline during the prepare call. The pid will be usable the next time a core comes online.
             */
            aggregate_state_t state;
            /** The mapping from event id to key */
            id_to_key_mappings_t mappings {};
            /** The set of cores that were previously online, but were removed as were detected as offline during the prepare call */
            std::set<core_no_t> offlined_cores {};
            /** The stream descriptors to monitor */
            std::vector<core_no_fd_pair_t> event_fds_by_core_no {};
        };

        /** Returned by pid_track_start */
        struct pid_track_start_result_t {
            /** Indicates the state of the pid, where:
             * - aggregate_state_t::usable means the pid was started correctly on at least one core.
             * - aggregate_state_t::failed means some unexpected fatal error occured. The pid will be reverted to an untracked state.
             * - aggregate_state_t::terminated means that the pid terminated during the prepare call and is reverted to an untracked state.
             * - aggregate_state_t::offline means no cores are currently online, or all that previously were online went offline during the prepare call. The pid will be usable the next time a core comes online.
             */
            aggregate_state_t state;
            /** The set of cores that were previously online, but were removed as were detected as offline during the start call */
            std::set<core_no_t> offlined_cores {};
        };

        static constexpr pid_t self_pid {0};
        static constexpr pid_t system_wide_pid {-1};
        static constexpr pid_t header_pid {0}; // not the same as system-wide, and not a valid pid

        /**
         * Construct a new active capture binding manager
         *
         * @param perf_activator The event *perf_activator object
         * @param configuration The counter configuration
         * @param uncore_pmus The uncore pmu list
         * @param core_no_to_spe_type The mapping from core no to SPE pmu type value
         * @param is_system_wide True for system-wide captures, false for app captures
         * @param enable_on_exec True for enable-on-exec with app captures
         */
        event_binding_manager_t(std::shared_ptr<perf_activator_t> perf_activator,
                                event_configuration_t const & configuration,
                                std::vector<perf_capture_configuration_t::uncore_pmu_t> const & uncore_pmus,
                                std::map<core_no_t, std::uint32_t> const & core_no_to_spe_type,
                                bool is_system_wide,
                                bool enable_on_exec)
            : perf_activator(std::move(perf_activator)),
              configuration(configuration),
              uncore_pmus(uncore_pmus),
              core_no_to_spe_type(core_no_to_spe_type),
              is_system_wide(is_system_wide),
              enable_on_exec(enable_on_exec)
        {
        }

        /** @return True if capture started, false otherwise */
        [[nodiscard]] bool is_capture_started() const { return capture_started; }

        /** Mark the capture as having started */
        void set_capture_started() { capture_started = true; }

        /** @return true if there are any SPE counters active on any core */
        [[nodiscard]] bool has_spe() const
        {
            return !(core_no_to_spe_type.empty() || configuration.spe_events.empty());
        }

        /** @return true if the cpu requires an aux buffer */
        [[nodiscard]] bool requires_aux(core_no_t no) const
        {
            // does it have any spe events?
            auto spe_it = core_no_to_spe_type.find(no);
            auto spe_type = (spe_it != core_no_to_spe_type.end() ? spe_it->second : 0);

            return spe_type != 0;
        }

        /**
         * Called to notify that a cpu core was onlined
         *
         * @param no The identifier for the core that changed state
         * @param cluster_id The cluster id for the core
         * @param additional_tids The set of new additional pids to add to the known set and activate on this core (only)
         */
        [[nodiscard]] core_online_prepare_result_t core_online_prepare(core_no_t no,
                                                                       cpu_cluster_id_t cluster_id,
                                                                       std::set<pid_t> const & additional_tids)
        {
            runtime_assert(additional_tids.empty() || !is_system_wide,
                           "additional_tids provided but system-wide capture");

            LOG_DEBUG("Core online prepare %d 0x%x", lib::toEnumValue(no), lib::toEnumValue(cluster_id));

            // update the set of tracked pids
            tracked_pids.insert(additional_tids.begin(), additional_tids.end());

            // update the core type map
            auto [it, inserted] = core_properties.try_emplace(no, core_properties_t {no, cluster_id});

            // if the core was already online, then fail
            if (!inserted) {
                LOG_DEBUG("Core already online");
                return core_online_prepare_result_t {aggregate_state_t::failed};
            }

            id_to_key_mappings_t id_to_key_mappings {};

            // tracking fds for polling / mmap
            std::shared_ptr<perf_ringbuffer_mmap_t> mmap_ptr {};
            std::vector<pid_fd_pair_t> event_fds_by_pid {};

            // create the per-mmap header event
            auto header_result = core_online_prepare_header(no, cluster_id, it);
            if (header_result.state != aggregate_state_t::usable) {
                return core_online_prepare_result_t {header_result.state};
            }

            // save the header id tracking
            id_to_key_mappings.emplace_back(header_result.id, configuration.header_event.key);
            // store the fd
            it->second.header_event_fd = header_result.fd;
            // mmap the header event
            mmap_ptr = std::make_shared<perf_ringbuffer_mmap_t>(
                perf_activator->mmap_data(no, header_result.fd->native_handle()));
            if (!mmap_ptr->has_data()) {
                LOG_WARNING("Core online prepare %d 0x%x failed due to data mmap error",
                            lib::toEnumValue(no),
                            lib::toEnumValue(cluster_id));
                core_offline_it(it);
                return core_online_prepare_result_t {aggregate_state_t::failed};
            }
            // store the mmap
            it->second.mmap = mmap_ptr;
            // header_fd should be in event_fds
            event_fds_by_pid.emplace_back(pid_fd_pair_t {header_pid, {header_result.fd, false}});

            // create the real events
            LOG_DEBUG("Creating core set %d 0x%x", lib::toEnumValue(no), lib::toEnumValue(cluster_id));
            auto [result, removed_pids] = create_binding_sets_for_core(
                [&id_to_key_mappings](gator_key_t key, perf_event_id_t id) {
                    id_to_key_mappings.emplace_back(id, key);
                },
                make_mmap_tracker(
                    perf_activator,
                    mmap_ptr,
                    header_result.fd,
                    no,
                    cluster_id,
                    [&event_fds_by_pid](pid_t pid, std::shared_ptr<stream_descriptor_t> fd, bool requires_aux) {
                        event_fds_by_pid.emplace_back(pid_fd_pair_t {pid, {std::move(fd), requires_aux}});
                    }),
                it->second);

            switch (result) {
                case aggregate_state_t::usable: {
                    LOG_FINE("Core online prepare %d 0x%x succeeded",
                             lib::toEnumValue(no),
                             lib::toEnumValue(cluster_id));

                    // now enable the header event
                    auto const started = perf_activator->start(header_result.fd->native_handle());
                    runtime_assert(started, "header event not started");

                    return core_online_prepare_result_t {result,
                                                         std::move(id_to_key_mappings),
                                                         std::move(removed_pids),
                                                         std::move(event_fds_by_pid),
                                                         mmap_ptr};
                }
                case aggregate_state_t::terminated: {
                    LOG_WARNING("Core online prepare %d 0x%x failed as all threads terminated / none tracked",
                                lib::toEnumValue(no),
                                lib::toEnumValue(cluster_id));

                    // now enable the header event
                    auto const started = perf_activator->start(header_result.fd->native_handle());
                    runtime_assert(started, "header event not started");

                    // return usable, but only have the header id mapping
                    return core_online_prepare_result_t {aggregate_state_t::usable,
                                                         {
                                                             {header_result.id, configuration.header_event.key},
                                                         },
                                                         std::move(removed_pids),
                                                         {pid_fd_pair_t {header_pid, {header_result.fd, false}}},
                                                         mmap_ptr};
                }
                case aggregate_state_t::offline: {
                    LOG_WARNING("Core online prepare %d 0x%x failed as core went offline",
                                lib::toEnumValue(no),
                                lib::toEnumValue(cluster_id));
                    break;
                }
                case aggregate_state_t::failed: {
                    LOG_WARNING("Core online prepare %d 0x%x failed due to error",
                                lib::toEnumValue(no),
                                lib::toEnumValue(cluster_id));
                    break;
                }
                default: {
                    throw std::runtime_error("unexpected aggregate_state_t");
                }
            }

            // for offline / failed - transition all the event sets into offline state
            core_offline_it(it);

            return core_online_prepare_result_t {result, {}, std::move(removed_pids)};
        }

        /**
         * Called to notify that a cpu core was onlined
         *
         * @param no The identifier for the core that changed state
         */
        [[nodiscard]] core_online_start_result_t core_online_start(core_no_t no)
        {
            runtime_assert(capture_started, "core_online_start called before capture started");

            // no operation required if the core is already offline
            auto it = core_properties.find(no);
            if (it == core_properties.end()) {
                LOG_DEBUG("Core online start %d called, but core offline", lib::toEnumValue(no));
                return {aggregate_state_t::offline, {}};
            }

            // if the core is online but there are no pids yet
            if (it->second.binding_sets.empty()) {
                LOG_DEBUG("Core online start %d called, but no pids are tracked", lib::toEnumValue(no));
                return {aggregate_state_t::terminated, {}};
            }

            // now transition all the event sets into online state
            bool all_terminated = true;

            auto & binding_sets = it->second.binding_sets;
            std::set<pid_t> terminated_pids {};

            for (auto & entry : binding_sets) {
                LOG_DEBUG("Core online start %d called, starting pid %d", lib::toEnumValue(no), entry.first);

                auto result = entry.second.start(*perf_activator);

                switch (result) {
                    case aggregate_state_t::usable: {
                        all_terminated = false;
                        break;
                    }

                    case aggregate_state_t::terminated: {
                        // the process is terminated, remove it (later)
                        LOG_DEBUG("Core online start %d called, pid %d was terminated",
                                  lib::toEnumValue(no),
                                  entry.first);
                        terminated_pids.insert(entry.first);
                        break;
                    }

                    case aggregate_state_t::offline:
                    case aggregate_state_t::failed: {
                        LOG_DEBUG("Core online start %d called, pid %d %s, removing core",
                                  lib::toEnumValue(no),
                                  entry.first,
                                  (result == aggregate_state_t::offline ? "was offline" : "failed with error"));

                        core_offline_it(it);
                        return {result, {}};
                    }

                    default: {
                        throw std::runtime_error("unexpected aggregate_state_t");
                    }
                }
            }

            // remove any terminated pids
            for (auto pid : terminated_pids) {
                pid_untrack(pid);
            }

            return {(all_terminated ? aggregate_state_t::terminated //
                                    : aggregate_state_t::usable),
                    std::move(terminated_pids)};
        }

        /**
         * Called to notify that a cpu core was offlined
         *
         * @param no The identifier for the core that changed state
         */
        void core_offline(core_no_t no)
        {
            LOG_DEBUG("Core offline %d", lib::toEnumValue(no));

            // no opperation required if the core is already offline
            auto it = core_properties.find(no);
            if (it == core_properties.end()) {
                return;
            }

            // offline and erase it
            core_offline_it(it);
        }

        /**
         * Add a new PID (a thread) to the set of threads that are currently being captured.
         *
         * If the capture is currently active, then they will be activated immediately, otherwise the PID is stored
         * and activated when the capture is started.
         */
        [[nodiscard]] pid_track_prepare_result_t pid_track_prepare(pid_t pid)
        {
            LOG_DEBUG("Track %d", pid);

            runtime_assert(!is_system_wide, "pid_track_prepare is only valid when !system-wide");

            auto [it, inserted] = tracked_pids.insert(pid);
            (void) it; // gcc7 :-(

            // if the pid is newly tracked then create bindings for all active cores
            if (!inserted) {
                LOG_DEBUG("Duplicate pid tracked");
                return {aggregate_state_t::usable, {}, {}};
            }

            id_to_key_mappings_t id_to_key_mappings {};
            std::vector<core_no_fd_pair_t> event_fds_by_core_no {};

            auto [result, offlined_cores] = create_binding_sets_for_pid(
                [&id_to_key_mappings](gator_key_t key, perf_event_id_t id) {
                    id_to_key_mappings.emplace_back(id, key);
                },
                event_fds_by_core_no,
                pid);

            switch (result) {
                case aggregate_state_t::usable: {
                    LOG_DEBUG("Track %d was succesfully prepared", pid);

                    return {aggregate_state_t::usable,
                            std::move(id_to_key_mappings),
                            std::move(offlined_cores),
                            std::move(event_fds_by_core_no)};
                }

                case aggregate_state_t::offline: {
                    LOG_DEBUG("Track %d was succesful, but all cores offline", pid);
                    return {result, {}, std::move(offlined_cores), {}};
                }

                case aggregate_state_t::terminated:
                case aggregate_state_t::failed: {
                    LOG_DEBUG("Track %d failed %s",
                              pid,
                              (result == aggregate_state_t::terminated ? "as process terminated"
                                                                       : "due to unexpected error"));

                    // remove the tracking and the bindings
                    tracked_pids.erase(pid);
                    remove_binding_sets_for_pid(pid);
                    return {result, {}, {}, {}};
                }

                default: {
                    throw std::runtime_error("unexpected aggregate_state_t");
                }
            }
        }

        /** Start binding sets on all known cores for the specified pid */
        [[nodiscard]] pid_track_start_result_t pid_track_start(pid_t pid)
        {
            runtime_assert(!is_system_wide, "pid_track_start is only valid when !system-wide");
            runtime_assert(capture_started, "pid_track_start called before capture started");

            // check pid is tracked
            if (tracked_pids.count(pid) == 0) {
                LOG_DEBUG("Start pid %d failed as pid terminated / not tracked", pid);
                return {aggregate_state_t::terminated, {}};
            }

            // now transition all the event sets into online state
            bool all_offline = true;
            std::set<core_no_t> offlined_cores {};

            for (auto & entry : core_properties) {
                auto it = entry.second.binding_sets.find(pid);
                if (it != entry.second.binding_sets.end()) {
                    LOG_DEBUG("Start pid %d on core %d", pid, lib::toEnumValue(entry.first));

                    auto result = it->second.start(*perf_activator);
                    switch (result) {
                        case aggregate_state_t::usable: {
                            all_offline = false;
                            break;
                        }

                        case aggregate_state_t::offline: {
                            LOG_DEBUG("Start pid %d on core %d failed as core offline",
                                      pid,
                                      lib::toEnumValue(entry.first));
                            offlined_cores.insert(entry.first);
                            break;
                        }

                        case aggregate_state_t::terminated:
                        case aggregate_state_t::failed: {
                            LOG_DEBUG("Start pid %d on core %d failed %s",
                                      pid,
                                      lib::toEnumValue(entry.first),
                                      (result == aggregate_state_t::terminated ? "as process terminated"
                                                                               : "due to unexpected error"));
                            //  transition all the event sets into offline state and remove them
                            remove_binding_sets_for_pid(pid);
                            return {result, {}};
                        }

                        default: {
                            throw std::runtime_error("unexpected aggregate_state_t");
                        }
                    }
                }
                else {
                    LOG_WARNING("Start pid %d on core %d failed as pid not found", pid, lib::toEnumValue(entry.first));
                }
            }

            // remove all offline cores
            for (auto no : offlined_cores) {
                core_offline(no);
            }

            return {(all_offline ? aggregate_state_t::offline //
                                 : aggregate_state_t::usable),
                    std::move(offlined_cores)};
        }

        /**
         * Remove a PID (if for example, the process exists) from the set of tracked pids.
         */
        void pid_untrack(pid_t pid)
        {
            LOG_DEBUG("Untrack %d", pid);

            if (tracked_pids.erase(pid) > 0) {
                remove_binding_sets_for_pid(pid);
            }
        }

    private:
        /** Used by the event_bindings to track fds and create the mmap */
        template<typename Consumer>
        struct mmap_tracker_t {
            std::shared_ptr<perf_activator_t> perf_activator;
            std::shared_ptr<perf_ringbuffer_mmap_t> mmap;
            std::shared_ptr<stream_descriptor_t> header_event_fd;
            core_no_t no;
            cpu_cluster_id_t cluster_id;
            Consumer consumer;

            [[nodiscard]] bool operator()(pid_t pid, std::shared_ptr<stream_descriptor_t> fd, bool requires_aux)
            {
                // save the fd to the list for monitoring
                consumer(pid, fd, requires_aux);

                if (!mmap->has_data()) {
                    LOG_DEBUG("Core online prepare %d 0x%x failed due to data mmap error",
                              lib::toEnumValue(no),
                              lib::toEnumValue(cluster_id));
                    return false;
                }

                // redirect output
                if (!perf_activator->set_output(fd->native_handle(), header_event_fd->native_handle())) {
                    LOG_DEBUG("Core online prepare %d 0x%x failed due to set_output error",
                              lib::toEnumValue(no),
                              lib::toEnumValue(cluster_id));
                    return false;
                }

                // mmap aux
                if (requires_aux) {
                    perf_activator->mmap_aux(*mmap, no, fd->native_handle());
                    if (!mmap->has_aux()) {
                        LOG_DEBUG("Core online prepare %d 0x%x failed due to mmap_aux failure",
                                  lib::toEnumValue(no),
                                  lib::toEnumValue(cluster_id));
                        return false;
                    }
                }

                return true;
            }
        };

        template<typename Consumer>
        static mmap_tracker_t<Consumer> make_mmap_tracker(std::shared_ptr<perf_activator_t> perf_activator,
                                                          std::shared_ptr<perf_ringbuffer_mmap_t> mmap,
                                                          std::shared_ptr<stream_descriptor_t> header_event_fd,
                                                          core_no_t no,
                                                          cpu_cluster_id_t cluster_id,
                                                          Consumer && consumer)
        {
            return mmap_tracker_t<Consumer> {std::move(perf_activator),
                                             std::move(mmap),
                                             std::move(header_event_fd),
                                             no,
                                             cluster_id,
                                             std::forward<Consumer>(consumer)};
        }

        /**
         * The set of core-specifc properties, including the core-type, and the binding sets for that core
         */
        struct core_properties_t {
            /** Store all the binding sets, by pid */
            std::map<pid_t, event_binding_set_type> binding_sets {};
            /** The set of uncore PMUs active on this CPU */
            std::set<uncore_pmu_id_t> active_uncore_pmu_ids {};
            /** The core number */
            core_no_t no;
            /** The core cluster id */
            cpu_cluster_id_t cluster_id;
            /** The mmap */
            std::shared_ptr<perf_ringbuffer_mmap_t> mmap {};
            /** The header event fd */
            std::shared_ptr<stream_descriptor_t> header_event_fd {};

            constexpr core_properties_t(core_no_t no, cpu_cluster_id_t cluster_id) : no(no), cluster_id(cluster_id) {}
        };

        std::shared_ptr<perf_activator_t> perf_activator;
        event_configuration_t const & configuration;
        std::vector<perf_capture_configuration_t::uncore_pmu_t> const & uncore_pmus;
        std::map<core_no_t, std::uint32_t> const & core_no_to_spe_type;
        std::map<core_no_t, core_properties_t> core_properties {};
        std::map<std::uint32_t, std::vector<event_definition_t>> spe_event_definitions_retyped {};
        bool is_system_wide;
        bool enable_on_exec;
        bool capture_started {false};
        std::set<pid_t> tracked_pids {};
        std::set<uncore_pmu_id_t> all_active_uncore_pmu_ids {};

        /**
         * Create the binding sets for some core.
         *
         * The subclass will override this and invoke create_binding_set as appropriate for each process id that it cares about (which may just be -1 in the case of system-wide).
         *
         * @param properties The core properties object from the core_properties map
         */
        template<typename IdToKeyMappingTracker, typename MmapTracker>
        [[nodiscard]] std::pair<aggregate_state_t, std::set<pid_t>> create_binding_sets_for_core(
            IdToKeyMappingTracker && id_to_key_mapping_tracker,
            MmapTracker && mmap_tracker,
            core_properties_t & properties)
        {
            LOG_DEBUG("Create for core %d", lib::toEnumValue(properties.no));

            // just forward on, with pid == -1 for system wide
            if (is_system_wide) {
                return {create_binding_set(id_to_key_mapping_tracker, mmap_tracker, properties, system_wide_pid), {}};
            }

            // if there are no pids yet
            if (tracked_pids.empty()) {
                return {aggregate_state_t::terminated, {}};
            }

            bool all_terminated = true;
            std::set<pid_t> terminated_pids {};

            // create a binding set for each known pid
            for (auto pid : tracked_pids) {
                auto result = create_binding_set(id_to_key_mapping_tracker, mmap_tracker, properties, pid);

                switch (result) {
                    case aggregate_state_t::usable: {
                        all_terminated = false;
                        break;
                    }

                    case aggregate_state_t::terminated: {
                        LOG_DEBUG("Core online prepare %d 0x%x detected a terminated process: %d",
                                  lib::toEnumValue(properties.no),
                                  lib::toEnumValue(properties.cluster_id),
                                  pid);

                        terminated_pids.insert(pid);
                        break;
                    }

                    case aggregate_state_t::offline:
                    case aggregate_state_t::failed: {
                        return {result, {}};
                    }

                    default: {
                        throw std::runtime_error("unexpected aggregate_state_t");
                    }
                }
            }

            // remove any terminated pids
            for (auto pid : terminated_pids) {
                pid_untrack(pid);
            }

            return {(all_terminated ? aggregate_state_t::terminated //
                                    : aggregate_state_t::usable),
                    std::move(terminated_pids)};
        }

        /**
         * Create a binding set object associated with some core and process id
         *
         * @param id_to_key_mapping_tracker Receives the id->key mappings for all valid events
         * @param mmap_tracker A callable of `bool(std::shared_ptr<stream_descriptor>, bool)` that creates/appends fds to the mmap, for later polling
         * @param properties The core properties object
         * @param pid The process id
         * @return the aggregate state of the attempt to create the events
         */
        template<typename IdToKeyMappingTracker, typename MmapTracker>
        [[nodiscard]] aggregate_state_t create_binding_set(IdToKeyMappingTracker && id_to_key_mapping_tracker,
                                                           MmapTracker && mmap_tracker,
                                                           core_properties_t & properties,
                                                           pid_t pid)
        {
            LOG_DEBUG("Create binding set no=%d :: pid=%d :: cluster=%d :: #events=%zu :: enable_on_exec=%u :: "
                      "capture_started=%u",
                      lib::toEnumValue(properties.no),
                      pid,
                      lib::toEnumValue(properties.cluster_id),
                      configuration.cluster_specific_events.size(),
                      enable_on_exec,
                      capture_started);

            // check the header fd and mmap
            runtime_assert(properties.mmap != nullptr, "Invalid mmap value");
            runtime_assert(properties.header_event_fd != nullptr, "Invalid header_event_fd value");

            // find the set of cluster events
            auto cluster_it = configuration.cluster_specific_events.find(properties.cluster_id);
            auto const * cluster_events =
                (cluster_it != configuration.cluster_specific_events.end() ? &(cluster_it->second) : nullptr);

            // and core specific events
            auto core_it = configuration.cpu_specific_events.find(properties.no);
            auto const * core_events =
                (core_it != configuration.cpu_specific_events.end() ? &(core_it->second) : nullptr);

            // and spe events
            auto spe_it = core_no_to_spe_type.find(properties.no);
            const auto spe_type = (spe_it != core_no_to_spe_type.end() ? spe_it->second : 0);

            // and uncore events
            auto [uncore_ids, uncore_event_count] = find_all_uncore_ids_for(properties.no, pid);

            // check there is any work to do
            auto has_no_events = configuration.global_events.empty()
                              && ((cluster_events == nullptr) || cluster_events->empty())
                              && ((core_events == nullptr) || core_events->empty())
                              && ((spe_type == 0) || configuration.spe_events.empty()) && (uncore_event_count == 0);

            if (has_no_events) {
                LOG_DEBUG("No events configured for cpu=%d, pid=%d", lib::toEnumValue(properties.no), pid);
                return aggregate_state_t::terminated;
            }

            // create the entry
            auto [it, inserted] = properties.binding_sets.try_emplace(pid, properties.no, pid);

            if (!inserted) {
                LOG_ERROR("A binding set already exists for cpu=%d, pid=%d", lib::toEnumValue(properties.no), pid);
                throw std::logic_error("Cannot create binding set for a process that already exists");
            }

            auto & binding_set = it->second;

            // first add all the global events
            if (!configuration.global_events.empty()) {
                auto result = binding_set.add_mixed(configuration.global_events);
                // this should be impossible since the group is new
                runtime_assert(result,
                               "Failed to add a global event configuration, perhaps the binding set is not offline");
            }

            // then add the cluster events
            if (cluster_events != nullptr) {
                for (auto const & [ndx, events] : *cluster_events) {
                    (void) ndx; // gcc7 :-(
                    runtime_assert(!events.empty(), "Cluster sub-group is unexpectedly empty");
                    auto result = binding_set.add_mixed(events);
                    // this should be impossible since the group is new
                    runtime_assert(
                        result,
                        "Failed to add a cluster event configuration, perhaps the binding set is not offline");
                }
            }

            if (core_events != nullptr) {
                auto result = binding_set.add_mixed(*core_events);
                // this should be impossible since the group is new
                runtime_assert(result,
                               "Failed to add a cpu event configuration, perhaps the binding set is not offline");
            }

            if ((!configuration.spe_events.empty()) && (spe_type > 0)) {
                auto result = binding_set.add_mixed(get_retyped_spe_definitions(spe_type));
                // this should be impossible since the group is new
                runtime_assert(result,
                               "Failed to add an SPE event configuration, perhaps the binding set is not offline");
            }

            for (auto id : uncore_ids) {
                auto result = binding_set.add_mixed(configuration.uncore_specific_events.at(id));
                // this should be impossible since the group is new
                runtime_assert(result,
                               "Failed to add an uncore event configuration, perhaps the binding set is not offline");
                properties.active_uncore_pmu_ids.insert(id);
                all_active_uncore_pmu_ids.insert(id);
            }

            // now all the bindings are created, now create the events
            auto result = binding_set.create_events(
                enable_on_exec && !capture_started,
                id_to_key_mapping_tracker,
                [pid, &mmap_tracker](std::shared_ptr<stream_descriptor_t> fd, bool requires_aux) {
                    return mmap_tracker(pid, std::move(fd), requires_aux);
                },
                *perf_activator,
                spe_type);

            switch (result) {
                case aggregate_state_t::usable: {
                    LOG_DEBUG("Create binding set for core=%d, pid=%d was successful",
                              lib::toEnumValue(properties.no),
                              pid);
                    return result;
                }

                case aggregate_state_t::offline:
                case aggregate_state_t::failed:
                case aggregate_state_t::terminated: {
                    LOG_DEBUG(
                        "Create binding set for core=%d, pid=%d failed due to %s",
                        lib::toEnumValue(properties.no),
                        pid,
                        (result == aggregate_state_t::offline
                             ? "core offline"
                             : (result == aggregate_state_t::terminated ? "process terminated" : "unexpected error")));
                    // erase the entry and return the result
                    properties.binding_sets.erase(it);
                    return result;
                }

                default: {
                    throw std::runtime_error("unexpected aggregate_state_t");
                }
            }
        }

        /** Find all uncore pmus associated with some core that need to be brought online */
        [[nodiscard]] std::pair<std::set<uncore_pmu_id_t>, std::size_t> find_all_uncore_ids_for(core_no_t no, pid_t pid)
        {
            std::pair<std::set<uncore_pmu_id_t>, std::size_t> result {{}, 0};

            // do nothing if the pid is not system-wide
            if (pid != system_wide_pid) {
                return result;
            }

            // iterate each uncore pmu and check for inactive uncores that are associated with the cpu
            for (auto const & [id, events] : configuration.uncore_specific_events) {
                // already active on another core?
                if (all_active_uncore_pmu_ids.count(id) > 0) {
                    LOG_DEBUG("Ignoring uncore %d on %d as already active", lib::toEnumValue(id), lib::toEnumValue(no));
                    continue;
                }

                auto const cpu_no = lib::toEnumValue(no);
                auto const index = lib::toEnumValue(id);
                runtime_assert((index >= 0) && (std::size_t(index) < uncore_pmus.size()), "Invalid uncore pmu id");
                auto const & pmu = uncore_pmus.at(index);
                auto const cpu_mask = perf_utils::readCpuMask(pmu.getId());
                auto const current_cpu_not_in_mask = ((!cpu_mask.empty()) && (cpu_mask.count(cpu_no) == 0));
                auto const mask_is_empty_and_cpu_not_default = (cpu_mask.empty() && (cpu_no != 0));

                // skip pmus not associated with this core
                if (current_cpu_not_in_mask || mask_is_empty_and_cpu_not_default) {
                    LOG_DEBUG("Ignoring uncore %d on %d as not selected (%zu / %u / %u)",
                              lib::toEnumValue(id),
                              lib::toEnumValue(no),
                              cpu_mask.size(),
                              current_cpu_not_in_mask,
                              mask_is_empty_and_cpu_not_default);
                    continue;
                }

                // found one
                LOG_DEBUG("Selecting uncore %d on %d", lib::toEnumValue(id), lib::toEnumValue(no));
                result.first.insert(id);
                result.second += events.size();
            }

            return result;
        }

        /** Create binding sets on all known cores for the specified pid */
        template<typename IdToKeyMappingTracker>
        [[nodiscard]] std::pair<aggregate_state_t, std::set<core_no_t>> create_binding_sets_for_pid(
            IdToKeyMappingTracker && id_to_key_mapping_tracker,
            std::vector<core_no_fd_pair_t> & event_fds_by_core_no,
            pid_t pid)
        {
            LOG_DEBUG("Create for pid %d", pid);

            bool all_offline = true;
            std::set<core_no_t> offline_cores {};

            for (auto & entry : core_properties) {
                // create the events
                auto result = create_binding_set(
                    id_to_key_mapping_tracker,
                    make_mmap_tracker(
                        perf_activator,
                        entry.second.mmap,
                        entry.second.header_event_fd,
                        entry.second.no,
                        entry.second.cluster_id,
                        [no = entry.second.no,
                         &event_fds_by_core_no](pid_t, std::shared_ptr<stream_descriptor_t> fd, bool requires_aux) {
                            event_fds_by_core_no.emplace_back(core_no_fd_pair_t {no, {std::move(fd), requires_aux}});
                        }),
                    entry.second,
                    pid);

                switch (result) {
                    case aggregate_state_t::usable: {
                        all_offline = false;
                        break;
                    }

                    case aggregate_state_t::offline: {
                        LOG_DEBUG("Track %d detected offline core %d", pid, lib::toEnumValue(entry.first));

                        // other cores may be online, so continue trying
                        offline_cores.insert(entry.first);
                        break;
                    }

                    case aggregate_state_t::terminated:
                    case aggregate_state_t::failed: { // erase the entry and return the result
                        remove_binding_sets_for_pid(pid);
                        return {result, {}};
                    }

                    default: {
                        throw std::runtime_error("unexpected aggregate_state_t");
                    }
                }
            }

            // remove all failed cores
            for (auto no : offline_cores) {
                core_offline(no);
            }

            return {(all_offline ? aggregate_state_t::offline //
                                 : aggregate_state_t::usable),
                    std::move(offline_cores)};
        }

        /** Offline and remove binding sets on all known cores for the specified pid */
        void remove_binding_sets_for_pid(pid_t pid)
        {
            LOG_DEBUG("Remove all for pid %d", pid);

            for (auto & entry : core_properties) {
                auto it = entry.second.binding_sets.find(pid);
                if (it != entry.second.binding_sets.end()) {
                    // first, offline it
                    it->second.offline(*perf_activator);
                    // then erase the set
                    entry.second.binding_sets.erase(it);
                }
            }
        }

        /**
         * Get (first create) a copy of the event defintions in configuration.spe_events, but with the attr.type field changed to match
         * the provided type parameter.
         *
         * @param type The type parameter to set for the event definitions
         * @return The vector of modified event definitions
         */
        std::vector<event_definition_t> const & get_retyped_spe_definitions(std::uint32_t type)
        {
            auto & result = spe_event_definitions_retyped[type];

            if (result.empty()) {
                for (auto const & event : configuration.spe_events) {
                    auto & inserted = result.emplace_back(event);
                    inserted.attr.type = type;
                }
            }

            return result;
        }

        /** Common code for offlining and removing a core entry */
        void core_offline_it(typename std::map<core_no_t, core_properties_t>::iterator it)
        {
            auto & binding_sets = it->second.binding_sets;

            //  transition all the event sets into offline state
            for (auto & entry : binding_sets) {
                entry.second.offline(*perf_activator);
            }

            // make sure to mark any uncores as inactive
            for (auto id : it->second.active_uncore_pmu_ids) {
                all_active_uncore_pmu_ids.erase(id);
            }

            // finally, close the header event explicitly (so that any thing waiting on it will be cancelled)
            auto fd = it->second.header_event_fd;
            if (fd != nullptr) {
                fd->close();
            }

            // finally, erase it, freeing up the entry in the map
            core_properties.erase(it);
        }

        /** returned by core_online_prepare_header */
        struct core_online_prepare_header_result_t {
            aggregate_state_t state;
            perf_event_id_t id {perf_event_id_t::invalid};
            std::shared_ptr<stream_descriptor_t> fd {};
        };

        /**
         * Prepare the header event that all the other events are expected to redirect their mmap events through
         */
        core_online_prepare_header_result_t core_online_prepare_header(
            core_no_t no,
            cpu_cluster_id_t cluster_id,
            typename std::map<core_no_t, core_properties_t>::iterator it)
        {
            using enable_state_t = typename perf_activator_t::enable_state_t;
            using event_creation_status_t = typename perf_activator_t::event_creation_status_t;
            using read_ids_status_t = typename perf_activator_t::read_ids_status_t;

            LOG_DEBUG("Creating core header %d 0x%x", lib::toEnumValue(no), lib::toEnumValue(cluster_id));
            auto header_result = perf_activator->create_event(configuration.header_event,
                                                              enable_state_t::disabled,
                                                              no,
                                                              (is_system_wide ? system_wide_pid : self_pid),
                                                              -1);

            switch (header_result.status) {
                case event_creation_status_t::failed_fatal:
                case event_creation_status_t::failed_invalid_pid:
                case event_creation_status_t::failed_invalid_device: {
                    LOG_DEBUG("Creating core header %d 0x%x failed.",
                              lib::toEnumValue(no),
                              lib::toEnumValue(cluster_id));
                    core_properties.erase(it);
                    return {aggregate_state_t::failed};
                }
                case event_creation_status_t::failed_offline: {
                    LOG_DEBUG("Creating core header %d 0x%x was offline.",
                              lib::toEnumValue(no),
                              lib::toEnumValue(cluster_id));
                    core_properties.erase(it);
                    return {aggregate_state_t::offline};
                }
                case event_creation_status_t::success: {
                    if (perf_activator->is_legacy_kernel_requires_id_from_read()) {
                        // ok, read it first
                        auto [status, ids] =
                            perf_activator->read_legacy_ids(configuration.header_event.attr.read_format,
                                                            header_result.fd->native_handle(),
                                                            1);
                        switch (status) {
                            case read_ids_status_t::failed_fatal: {
                                LOG_DEBUG("Creating core header %d 0x%x failed to read id.",
                                          lib::toEnumValue(no),
                                          lib::toEnumValue(cluster_id));
                                core_properties.erase(it);
                                return {aggregate_state_t::failed};
                            }
                            case read_ids_status_t::failed_offline: {
                                LOG_DEBUG("Creating core header %d 0x%x failed to read id as offline.",
                                          lib::toEnumValue(no),
                                          lib::toEnumValue(cluster_id));
                                core_properties.erase(it);
                                return {aggregate_state_t::offline};
                            }
                            case read_ids_status_t::success: {
                                header_result.perf_id = ids.at(0);
                                break;
                            }
                        };
                    }

                    return {aggregate_state_t::usable, header_result.perf_id, header_result.fd};
                }
                default: {
                    throw std::runtime_error("unexpected aggregate_state_t");
                }
            }
        }
    };
}
