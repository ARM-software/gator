/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "agents/perf/capture_configuration.h"
#include "agents/perf/events/event_configuration.hpp"
#include "agents/perf/events/perf_event_utils.hpp"
#include "agents/perf/events/perf_ringbuffer_mmap.hpp"
#include "agents/perf/events/types.hpp"
#include "lib/Syscall.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/system/error_code.hpp>

#include <sys/mman.h>
#include <unistd.h>

namespace agents::perf {
    /**
     * Interface for object used to create and manipulate raw perf events
     */
    class perf_activator_t {
    public:
        /** Configures how/when events should be enabled */
        enum class enable_state_t {
            /** Event is created in a disabled state */
            disabled,
            /** Event is created in a disabled state with enable_on_exec set */
            enable_on_exec,
            /** Event is created in an enabled state */
            enabled,
        };

        /** Enumerates event creation result status possibilities */
        enum class event_creation_status_t {
            /** The event creation failed due to some error */
            failed_fatal,
            /** The event creation failed because the target core was offline */
            failed_offline,
            /** The event creation failed because the target pid was invalid */
            failed_invalid_pid,
            /** The event creation failed because the event was not supported on the specified pmu (or cpu) */
            failed_invalid_device,
            /** The event creation succeeded */
            success,
        };

        /** Returend as part of read_legacy_ids to indicate result status */
        enum class read_ids_status_t {
            /** Reading the ids failed with a fatal error */
            failed_fatal,
            /** Reading the ids failed because the core was offline */
            failed_offline,
            /** Reading the ids succeeded */
            success,
        };

        /** The stream type */
        using stream_descriptor_t = boost::asio::posix::stream_descriptor;

        /** Event creation result tuple returned by the create_event function */
        struct event_creation_result_t {
            /** The event ID, or invalid. Only meaningful when (failed == false) */
            perf_event_id_t perf_id {perf_event_id_t::invalid};
            /** The event file descriptor. Only meaningful when (failed == false) && (perf_id != invalid) */
            std::shared_ptr<stream_descriptor_t> fd {};
            /** The result status */
            event_creation_status_t status {event_creation_status_t::failed_fatal};
            /** The errno value returned by perf_event_open, if status is failed_fatal */
            boost::system::error_code perf_errno {};
            /** An optional error message for failed_fatal */
            std::optional<std::string> error_message {};

            explicit event_creation_result_t(event_creation_status_t status) : status(status) {}

            explicit event_creation_result_t(boost::system::error_code perf_errno,
                                             std::optional<std::string> error_message = {})
                : perf_errno(perf_errno), error_message(std::move(error_message))
            {
            }

            event_creation_result_t(perf_event_id_t perf_id, std::shared_ptr<stream_descriptor_t> fd)
                : perf_id(perf_id), fd(std::move(fd)), status(event_creation_status_t::success)
            {
            }
        };

        perf_activator_t(std::shared_ptr<perf_capture_configuration_t> conf, boost::asio::io_context & context)
            : capture_configuration(std::move(conf)),
              context(context),
              perf_event_printer(capture_configuration->cpuid_to_core_name,
                                 capture_configuration->per_core_cpuids,
                                 capture_configuration->perf_pmu_type_to_name)
        {
        }

        /** @return True if the kernel is old and requires using 'read' to determine the ID of events in a group */
        [[nodiscard]] bool is_legacy_kernel_requires_id_from_read() const;

        /**
         * Using the legacy method, read the IDs for a set of one or more events in a group.
         *
         * @param read_format The read_format for the group leader (or single) attribute
         * @param group_fd The group (or single event) file descriptor
         * @param nr_ids The number of events in the group (which must be >= 1)
         * @return a pair containing the status code and a vector containing some ids being the ids read
         */
        [[nodiscard]] static std::pair<read_ids_status_t, std::vector<perf_event_id_t>>
        read_legacy_ids(std::uint64_t read_format, int group_fd, std::size_t nr_ids);

        /**
         * Create the new event, but do not start it. The event is created in a disabled state, and its fd and perf id are returned.
         *
         * @param event The event to create
         * @param enable_state Configures the disable and enable_on_exec flags for the event
         * @param core_no The CPU no to use for the event
         * @param pid The PID to use for the event
         * @param group_fd The group leader fd, or -1
         * @return A `activation_result_t`, containing the status flag, perf id and file descriptor.
         *         When the status flag is failed_fatal, the perf id and fd must be ignored and a fatal
         *             error occured and the binding will move to failed state. `perf_errno` will be set to
         *             reflect the error state after the call to perf_event_open.
         *         When the status flag is failed_offline, the perf id and fd must be ignored and a
         *             non-fatal error occured and the binding will remain in offline state.
         *         When the status flag is failed_invalid_pid, the perf id and fd must be ignored and
         *             a possibly non-fatal error occured due to the pid provided was not valid, and the
         *             binding will move to terminated or failed state.
         *         When the status flag is failed_invalid_device, the perf id and fd must be ignored and
         *             a possibly non-fatal error occured due to perf_event_open returning ENOENT, and
         *             the binding will move to terminated or failed state.
         *         Otherwise, the status flag is success and the perf id and fd is set and the event is successfully created and the binding can move to ready state.
         */
        [[nodiscard]] event_creation_result_t create_event(event_definition_t const & event,
                                                           enable_state_t enable_state,
                                                           core_no_t core_no,
                                                           pid_t pid,
                                                           int group_fd);

        /**
         * Redirect mmap output from one fd to another
         *
         * @param fd The fd to redirect
         * @param output_fd the target fd
         * @return true succes
         * @return false failure
         */
        [[nodiscard]] bool set_output(int fd, int output_fd);

        /**
         * MMap the ringbuffer for the provided file descriptor.
         *
         * @note This method is only for the data region, use
         * mmap_aux(perf_ringbuffer_mmap_t const & mmap, core_no_t core_no, int fd) for the aux region
         * @param core_no The core no associated with the mapping
         * @param fd The event file descriptor
         * @return The mapping object
         */
        [[nodiscard]] perf_ringbuffer_mmap_t mmap_data(core_no_t core_no, int fd);

        /**
         * MMaps the aux region for the provided file descriptor.
         *
         * @note This method is only for the aux region, use mmap_data(core_no_t core_no, int fd) for the data region
         * @param mmap Reference to ringbuffer mmap object, this will be updated with the new region if mmap-ing is
         * successful
         * @param core_no The core no associated with the mapping
         * @param fd The event file descriptor
         */
        void mmap_aux(perf_ringbuffer_mmap_t & mmap, core_no_t core_no, int fd);

        /**
         * Enable the event, so that it starts producing data.
         *
         * @param fd The event file descriptor
         * @retval true  The event was enabled without error
         * @retval false The event could not be enabled due to some error (which usually implies the event is invalid)
         */
        [[nodiscard]] bool start(int fd);

        /**
         * Disable the event, so that it stops producing data (but it is not removed, so could be started again).
         *
         * @param fd The event file descriptor
         * @retval true  The event was disabled without error
         * @retval false The event could not be disabled due to some error (which usually implies the event is invalid)
         */
        bool stop(int fd);

        /**
         * Re-enable a single event (for example an AUX fd that was disabled on buffer full)
         *
         * @param fd The event file descriptor
         * @retval true  The event was enabled without error
         * @retval false The event could not be enabled due to some error (which usually implies the event is invalid)
         */
        bool re_enable(int fd);

    private:
        std::shared_ptr<perf_capture_configuration_t> capture_configuration;
        boost::asio::io_context & context;
        perf_event_printer_t perf_event_printer;
    };
}
