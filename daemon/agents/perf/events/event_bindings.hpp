/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */

#pragma once

#include "agents/perf/events/event_configuration.hpp"
#include "agents/perf/events/perf_activator.hpp"
#include "agents/perf/events/types.hpp"
#include "k/perf_event.h"
#include "lib/Assert.h"
#include "lib/EnumUtils.h"
#include "lib/Span.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

#include <boost/asio/posix/stream_descriptor.hpp>

namespace agents::perf {

    /** Enumerates possible states for each binding */
    enum class event_binding_state_t {
        /** The event has not been created or enabled */
        offline,
        /** The event has been created (fd & perf if is valid), but it has not been activated yet */
        ready,
        /** The event has been activated and is collecting data */
        online,
        /** The evemt could not be created/activated due to some fatal error */
        failed,
        /** The event was terminate (for example because the process being tracked has exited) */
        terminated,
        /** The event was not supported on the given pmu */
        not_supported,
    };

    /** Enumerate possible states for the aggregate bindings */
    enum class aggregate_state_t {
        /** All bindings are offline */
        offline,
        /** At least some bindings are ready/online */
        usable,
        /** All bindings are failed (or mix of failed / terminated) */
        failed,
        /** All bindings are terminated */
        terminated,
    };

    /**
     * An Event binding represents a single instance of a perf event, linking the event specification in the perf_event_attr, and gator key
     * to its event fd and perf id. Each binding is for a single core+pid only.
     *
     * Bindings have state representing whether or not the event has been created, enabled/disabled, or failed.
     * Events start in 'offline' state and are transitioned to 'ready' by the `create_event` method.
     * Once an event is 'ready' it may be activated for data collection by the `start` method, moving it to the 'online' state.
     * The event may also be disabled (pausing data collection) by the `stop` method, which will move the event back from 'online' to 'ready'.
     * In any case where the even cannot be created/activated because of a fatal error, the event moves to the 'failed' state.
     * The event may also be moved to the 'offline' state by the `offline` method, which fully deletes the perf event, closing any associated fd.
     */
    template<typename StreamDescriptor>
    class event_binding_t {
    public:
        using stream_descriptor_t = StreamDescriptor;

        explicit event_binding_t(event_definition_t const & event) : event(event)
        {
            runtime_assert((event.attr.read_format & PERF_FORMAT_ID) == PERF_FORMAT_ID, "PERF_FORMAT_ID is required");
        }

        /** @return the key associated with the event */
        [[nodiscard]] gator_key_t get_key() const { return event.key; }

        /** @return the perf id associated with the event */
        [[nodiscard]] perf_event_id_t get_id() const { return perf_id; }

        /** @return the file descriptor associated with the event */
        [[nodiscard]] int get_fd() { return (fd ? fd->native_handle() : -1); }

        /** @return the read_format for the event attribute */
        [[nodiscard]] std::uint64_t get_read_format() const { return event.attr.read_format; }

        /** @return true if the event is in the offline state, or false otherwise */
        [[nodiscard]] bool is_offline() const { return state == event_binding_state_t::offline; }

        /** @return true for pmu events */
        [[nodiscard]] bool is_pmu_event() const
        {
            return (event.attr.type == PERF_TYPE_HARDWARE) //
                || (event.attr.type == PERF_TYPE_RAW)      //
                || (event.attr.type == PERF_TYPE_HW_CACHE) //
                || (event.attr.type >= PERF_TYPE_MAX);
        }

        /** Set the event id as read from the legacy read id method */
        void set_id(perf_event_id_t id) { perf_id = id; }

        /**
         * Attempt to online this binding.
         * If the binding is offline, then it is transitioned to ready.
         * If the binding is ready or online, nothing happens.
         * If the binding is in a failed state, or creating fails, then it will be put in / stay in failed state.
         * @return The new state
         */
        template<typename MmapTracker, typename PerfActivator>
        [[nodiscard]] event_binding_state_t create_event(bool enable_on_exec,
                                                         int group_fd,
                                                         MmapTracker && mmap_tracker,
                                                         PerfActivator && activator,
                                                         core_no_t core_no,
                                                         pid_t pid,
                                                         std::uint32_t spe_type)
        {

            switch (state) {
                case event_binding_state_t::offline: {
                    // attempt to create, updating state
                    return (state = do_create_event(enable_on_exec, //
                                                    group_fd,
                                                    mmap_tracker,
                                                    activator,
                                                    core_no,
                                                    pid,
                                                    spe_type));
                }
                case event_binding_state_t::ready:
                case event_binding_state_t::online:
                case event_binding_state_t::failed:
                case event_binding_state_t::terminated:
                case event_binding_state_t::not_supported:
                    return state;
                default:
                    throw std::runtime_error("unexpected event_binding_state_t");
            }
        }

        /** Start the event if it is read, transitioning to 'online' */
        template<typename PerfActivator>
        [[nodiscard]] event_binding_state_t start(PerfActivator && activator)
        {
            switch (state) {
                case event_binding_state_t::ready: {
                    // start the event
                    auto result = activator.start(fd->native_handle());

                    if ((!result) && (fd->native_handle() == -1)) {
                        LOG_DEBUG("Raced against fd->close(), ignoring failure to start");
                        return event_binding_state_t::online;
                    }

                    // update the state and return it
                    state = (result ? event_binding_state_t::online : event_binding_state_t::failed);
                    return state;
                }

                case event_binding_state_t::offline:
                case event_binding_state_t::online:
                case event_binding_state_t::failed:
                case event_binding_state_t::terminated:
                case event_binding_state_t::not_supported: {
                    // no change required
                    return state;
                }

                default:
                    throw std::runtime_error("unexpected event_binding_state_t");
            }
        }

        /** Clean up all data and move back to 'offline' or 'failed' state */
        template<typename PerfActivator>
        void stop(PerfActivator && activator, bool failed)
        {
            // stop it if it is running
            if (fd) {
                activator.stop(fd->native_handle());
            }

            // clear state
            this->fd.reset();
            this->perf_id = perf_event_id_t::invalid;
            this->state = (failed ? event_binding_state_t::failed : event_binding_state_t::offline);
        }

    private:
        /** Does the attr require an aux buffer ? */
        static constexpr bool requires_aux(std::uint64_t spe_type, std::uint64_t attr_type)
        {
            return ((attr_type >= PERF_TYPE_MAX) && (attr_type == spe_type));
        }

        /** The attribute structure */
        event_definition_t const & event;
        /** The current state of the binding */
        event_binding_state_t state {event_binding_state_t::offline};
        /** The id allocated by perf */
        perf_event_id_t perf_id = perf_event_id_t::invalid;
        /** The opened file descriptor */
        std::shared_ptr<stream_descriptor_t> fd {};

        /** Perform the online, returning the new state */
        template<typename MmapTracker, typename PerfActivator>
        [[nodiscard]] event_binding_state_t do_create_event(bool enable_on_exec,
                                                            int group_fd,
                                                            MmapTracker && mmap_tracker,
                                                            PerfActivator && activator,
                                                            core_no_t core_no,
                                                            pid_t pid,
                                                            std::uint32_t spe_type)
        {
            // never enable it (Streamline expects the id->key map to be received before any ringbuffer data)
            const auto enable_state = (enable_on_exec ? perf_activator_t::enable_state_t::enable_on_exec
                                                      : perf_activator_t::enable_state_t::disabled);
            // do the activation
            const auto result = activator.create_event(event, enable_state, core_no, pid, group_fd);

            switch (result.status) {
                case perf_activator_t::event_creation_status_t::success: {
                    // add it to the mmap
                    if (!mmap_tracker(result.fd, requires_aux(spe_type, event.attr.type))) {
                        return event_binding_state_t::failed;
                    }
                    // success
                    this->perf_id = result.perf_id;
                    this->fd = std::move(result.fd);
                    return event_binding_state_t::ready;
                }

                case perf_activator_t::event_creation_status_t::failed_offline:
                    return event_binding_state_t::offline;

                case perf_activator_t::event_creation_status_t::failed_invalid_device:
                    return event_binding_state_t::not_supported;

                case perf_activator_t::event_creation_status_t::failed_invalid_pid:
                    return event_binding_state_t::terminated;

                case perf_activator_t::event_creation_status_t::failed_fatal:
                    return event_binding_state_t::failed;

                default:
                    throw std::runtime_error("unexpected event_creation_status_t");
            }
        }
    };

    /**
     * Represents a group of one or more event bindings collected into a perf event group
     */
    template<typename StreamDescriptor>
    class event_binding_group_t {
    public:
        using event_binding_type = event_binding_t<StreamDescriptor>;

        event_binding_group_t(event_definition_t const & leader, lib::Span<event_definition_t const> children)
        {
            runtime_assert(children.empty() || ((leader.attr.read_format & PERF_FORMAT_GROUP) == PERF_FORMAT_GROUP),
                           "Must be a stand alone attribute, or PERF_FORMAT_GROUP is required");

            // insert the leader as the first item
            bindings.emplace_back(leader);
            // and the children follow it
            for (auto const & child : children) {
                bindings.emplace_back(child);
            }
        }

        /** Insert another child event into the group */
        [[nodiscard]] bool add_event(event_definition_t const & event)
        {
            if (!bindings.front().is_offline()) {
                return false;
            }

            bindings.emplace_back(event);
            return true;
        }

        /**
         * Create all event bindings in the group
         */
        template<typename IdToKeyMappingTracker, typename MmapTracker, typename PerfActivator>
        [[nodiscard]] aggregate_state_t create_events(bool enable_on_exec,
                                                      IdToKeyMappingTracker && id_to_key_mapping_tracker,
                                                      MmapTracker && mmap_tracker,
                                                      PerfActivator && activator,
                                                      core_no_t core_no,
                                                      pid_t pid,
                                                      std::uint32_t spe_type)
        {
            auto const legacy_id_from_read = activator.is_legacy_kernel_requires_id_from_read();
            std::vector<event_binding_type *> bindings_for_id_read {};

            // first activate the leader
            auto & leader = bindings.front();
            auto const is_group_of_one_pmu = (this->bindings.size() == 1) && leader.is_pmu_event();

            auto leader_state =
                leader.create_event(enable_on_exec, -1, mmap_tracker, activator, core_no, pid, spe_type);
            switch (leader_state) {
                case event_binding_state_t::ready:
                    if (legacy_id_from_read) {
                        bindings_for_id_read.emplace_back(&leader);
                    }
                    else {
                        id_to_key_mapping_tracker(leader.get_key(), leader.get_id());
                    }
                    break;

                case event_binding_state_t::offline:
                    return aggregate_state_t::offline;

                case event_binding_state_t::not_supported:
                    return (is_group_of_one_pmu ? aggregate_state_t::usable : aggregate_state_t::offline);

                case event_binding_state_t::online:
                    return aggregate_state_t::usable;

                case event_binding_state_t::terminated:
                    return aggregate_state_t::terminated;

                case event_binding_state_t::failed:
                    return aggregate_state_t::failed;

                default:
                    throw std::runtime_error("unexpected event_binding_state_t");
            }

            auto const group_fd = leader.get_fd();

            // nnw activate any children
            for (std::size_t n = 1; n < bindings.size(); ++n) {
                auto & child = bindings.at(n);
                auto child_state =
                    child.create_event(enable_on_exec, group_fd, mmap_tracker, activator, core_no, pid, spe_type);
                switch (child_state) {
                    case event_binding_state_t::ready:
                    case event_binding_state_t::online:
                        if (legacy_id_from_read) {
                            bindings_for_id_read.emplace_back(&child);
                        }
                        else {
                            id_to_key_mapping_tracker(child.get_key(), child.get_id());
                        }
                        break;

                    case event_binding_state_t::terminated:
                        // the process terminated, just deactive all events
                        return destroy_events(activator, n + 1, aggregate_state_t::terminated);

                    case event_binding_state_t::offline:
                        // the core went offline, just deactive all events
                        return destroy_events(activator, n + 1, aggregate_state_t::offline);

                    case event_binding_state_t::failed:
                        // the event failed with unexpected error, offline all events and return failed
                        return destroy_events(activator, n + 1, aggregate_state_t::failed);

                    case event_binding_state_t::not_supported:
                        // ignored for non-group leaders as usually means legacy bigLITTLE setup
                        break;

                    default:
                        throw std::runtime_error("unexpected event_binding_state_t");
                }
            }

            if (!legacy_id_from_read) {
                return aggregate_state_t::usable;
            }

            // now get any ids
            auto [status, ids] =
                activator.read_legacy_ids(leader.get_read_format(), group_fd, bindings_for_id_read.size());

            switch (status) {
                case perf_activator_t::read_ids_status_t::success: {
                    // the sizes should match; if not it indicates an unexpected error
                    if (ids.size() != bindings_for_id_read.size()) {
                        return destroy_events(activator, bindings.size(), aggregate_state_t::failed);
                    }

                    // map the ids to the bindings
                    for (std::size_t n = 0; n < bindings_for_id_read.size(); ++n) {
                        // update the binding
                        auto * binding = bindings_for_id_read[n];
                        binding->set_id(ids[n]);
                        // and add to the tracker
                        id_to_key_mapping_tracker(binding->get_key(), binding->get_id());
                    }

                    return aggregate_state_t::usable;
                }

                case perf_activator_t::read_ids_status_t::failed_fatal:
                    return destroy_events(activator, bindings.size(), aggregate_state_t::failed);

                case perf_activator_t::read_ids_status_t::failed_offline:
                    return destroy_events(activator, bindings.size(), aggregate_state_t::offline);

                default:
                    throw std::runtime_error("unexpected read_ids_status_t");
            }
        }

        /** Start the events if it is read, transitioning to 'online' */
        template<typename PerfActivator>
        [[nodiscard]] aggregate_state_t start(PerfActivator && activator)
        {

            // first activate the leader
            auto & leader = bindings.front();
            auto const is_group_of_one_pmu = (this->bindings.size() == 1) && leader.is_pmu_event();
            auto result = leader.start(activator);

            switch (result) {
                case event_binding_state_t::offline:
                    return aggregate_state_t::offline;

                case event_binding_state_t::not_supported:
                    return (is_group_of_one_pmu ? aggregate_state_t::usable : aggregate_state_t::offline);

                case event_binding_state_t::online:
                    return aggregate_state_t::usable;

                case event_binding_state_t::failed:
                    return aggregate_state_t::failed;

                case event_binding_state_t::terminated:
                    return aggregate_state_t::terminated;

                case event_binding_state_t::ready:
                default:
                    throw std::runtime_error("unexpected event_binding_state_t");
            }
        }

        /** Clean up all data and move back to 'offline' or 'failed' state */
        template<typename PerfActivator>
        void stop(PerfActivator && activator, bool failed)
        {
            for (auto & binding : bindings) {
                binding.stop(activator, failed);
            }
        }

    private:
        std::vector<event_binding_type> bindings {};

        /**
         * Destroy any events previously created and return an error
         *
         * @param activator The activator used to stop the events
         * @param down_from The last event that was created (all bindings < down_from will be destroyed, with the assumption that all later ones are not yet created)
         * @param reason The failure reason
         * @return The aggregate state value
         */
        template<typename PerfActivator>
        [[nodiscard]] aggregate_state_t destroy_events(PerfActivator && activator,
                                                       std::size_t down_from,
                                                       aggregate_state_t reason)
        {
            for (std::size_t n = 0; n < down_from; ++n) {
                bindings.at(n).stop(activator, reason == aggregate_state_t::failed);
            }

            return reason;
        }
    };

    /**
     * Maintains a set of event binding groups that are all associated with the same core / pid.
     * Allows transitioning of bindings as a unit from one state to another
     */
    template<typename StreamDescriptor>
    class event_binding_set_t {
    public:
        using event_binding_group_type = event_binding_group_t<StreamDescriptor>;

        /**
         * Construct a new per core event bindings object, for a set of events associated with the provided core, and pid
         *
         * @param core_no The core no for all events (may be -1 as per perf_event_open)
         * @param pid  The pid for all events (may be -1 or 0 as per perf_event_open)
         */
        event_binding_set_t(core_no_t core_no, pid_t pid) : core_no(core_no), pid(pid) {}

        /** Add a stand alone event
         * @retval true if the event was successfully added
         * @retval false if the event was not added (e.g. because the bindings were not offline)
         */
        [[nodiscard]] bool add_event(event_definition_t const & event)
        {
            if (state != aggregate_state_t::offline) {
                return false;
            }

            groups.emplace_back(event, lib::Span<event_definition_t const> {});
            return true;
        }

        /** Add a group of events, where the first item in the span is the leader.
         * @retval true if the events were successfully added
         * @retval false if the events were not added (e.g. because the bindings were not offline, or the span is empty)
         */
        [[nodiscard]] bool add_group(lib::Span<event_definition_t const> events)
        {
            if ((state != aggregate_state_t::offline) || events.empty()) {
                return false;
            }

            groups.emplace_back(events.front(), events.subspan(1));
            return true;
        }

        /**
         * Give some event definitions, where the first is a group leader and the rest are
         * a mix of stand alone events and members of that group, create the appropriate
         * events and groups and add them to the set
         *
         * @retval true if the events were successfully added
         * @retval false if the events wer not added (e.g. because the bindings were not offline, or the span is empty)
         */
        [[nodiscard]] bool add_mixed(lib::Span<event_definition_t const> events)
        {
            if ((state != aggregate_state_t::offline) || events.empty()) {
                return false;
            }

            runtime_assert(is_group_leader(events.front()), "First item must be group leader");

            auto & group = groups.emplace_back(events.front(), lib::Span<event_definition_t const>());

            for (auto const & event : events.subspan(1)) {
                if (is_stand_alone(event)) {
                    groups.emplace_back(event, lib::Span<event_definition_t const>());
                }
                else {
                    auto result = group.add_event(event);
                    runtime_assert(result, "expected event to be inserted into new group");
                }
            }

            return true;
        }

        /** @return the current state */
        [[nodiscard]] aggregate_state_t get_state() const noexcept { return state; }

        /** Attempt to ready all the event bindings. */
        template<typename IdToKeyMappingTracker, typename MmapTracker, typename PerfActivator>
        [[nodiscard]] aggregate_state_t create_events(bool enable_on_exec,
                                                      IdToKeyMappingTracker && id_to_key_mapping_tracker,
                                                      MmapTracker && mmap_tracker,
                                                      PerfActivator && activator,
                                                      std::uint32_t spe_type)
        {
            bool any_usable = false;

            for (std::size_t n = 0; n < groups.size(); ++n) {
                auto & group = groups.at(n);
                auto result = group.create_events(enable_on_exec,
                                                  id_to_key_mapping_tracker,
                                                  mmap_tracker,
                                                  activator,
                                                  core_no,
                                                  pid,
                                                  spe_type);
                switch (result) {
                    case aggregate_state_t::usable:
                        any_usable = true;
                        break;
                    case aggregate_state_t::terminated:
                    case aggregate_state_t::offline:
                    case aggregate_state_t::failed:
                        return (state = destroy_groups(activator, n + 1, result));

                    default:
                        throw std::runtime_error("unexpected aggregate_state_t");
                }
            }

            return (state = (any_usable ? aggregate_state_t::usable : aggregate_state_t::terminated));
        }

        /**  Attempt to online all the event bindings. */
        template<typename PerfActivator>
        [[nodiscard]] aggregate_state_t start(PerfActivator && activator)
        {
            bool any_usable = false;

            for (auto & group : groups) {
                auto result = group.start(activator);
                switch (result) {
                    case aggregate_state_t::usable:
                        any_usable = true;
                        break;
                    case aggregate_state_t::terminated:
                    case aggregate_state_t::offline:
                    case aggregate_state_t::failed:
                        return (state = destroy_groups(activator, groups.size(), result));

                    default:
                        throw std::runtime_error("unexpected aggregate_state_t");
                }
            }

            return (state = (any_usable ? aggregate_state_t::usable : aggregate_state_t::terminated));
        }

        /** Clean up all data and move back to 'offline' state. */
        template<typename PerfActivator>
        void offline(PerfActivator && activator)
        {
            for (auto & group : groups) {
                group.stop(activator, false);
            }
            state = aggregate_state_t::offline;
        }

    private:
        [[nodiscard]] static constexpr bool is_group_leader(event_definition_t const & event)
        {
            return event.attr.pinned;
        }

        [[nodiscard]] static constexpr bool is_stand_alone(event_definition_t const & event)
        {
            return event.attr.pinned;
        }

        std::vector<event_binding_group_type> groups {};
        core_no_t core_no;
        pid_t pid;
        aggregate_state_t state {aggregate_state_t::offline};

        /**
         * Destroy any groups previously created and return an error
         *
         * @param down_from The last group that was created (all group < down_from will be destroyed, with the assumption that all later ones are not yet created)
         * @param reason The faiure reason
         * @return The aggregate state value
         */
        template<typename PerfActivator>
        [[nodiscard]] aggregate_state_t destroy_groups(PerfActivator && activator,
                                                       std::size_t down_from,
                                                       aggregate_state_t reason)
        {
            for (std::size_t n = 0; n < down_from; ++n) {
                groups.at(n).stop(activator, reason == aggregate_state_t::failed);
            }
            return reason;
        }
    };
}
