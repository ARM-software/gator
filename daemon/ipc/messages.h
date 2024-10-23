/* Copyright (C) 2021-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "Time.h"
#include "ipc/message_key.h"
#include "ipc/message_traits.h"
#include "ipc/proto/generated/capture_configuration.pb.h"
#include "message_key.h"
#include "monotonic_pair.h"

#include <string_view>
#include <variant>

#include <boost/mp11/list.hpp>

namespace ipc {
    using annotation_uid_t = int;

    struct [[gnu::packed]] cpu_state_change_t {
        monotonic_delta_t monotonic_delta;
        int core_no;
        bool online;

        friend constexpr bool operator==(cpu_state_change_t const & a, cpu_state_change_t const & b)
        {
            return (a.monotonic_delta == b.monotonic_delta) && (a.core_no == b.core_no) && (a.online == b.online);
        }

        friend constexpr bool operator!=(cpu_state_change_t const & a, cpu_state_change_t const & b)
        {
            return !(a == b);
        }
    };

    enum class capture_failed_reason_t : std::uint8_t {
        /** Capture failed due to command exec failure */
        command_exec_failed,
        wait_for_cores_ready_failed,
    };

    /**
     * Helper template that associates a type name with a type so that it can be used at
     * runtime - e.g. to aid troubleshooting with log messages.
     */
    template<typename T>
    struct named_message_t;

    template<>
    struct named_message_t<std::monostate> {
        static constexpr std::string_view name {"unknown"};
    };

#define DEFINE_NAMED_MESSAGE(class)                                                                                    \
    template<>                                                                                                         \
    struct named_message_t<class> {                                                                                    \
        static constexpr std::string_view name {#class};                                                               \
    };

    /**
     * Helper function to return the message type name from a message instance.
     *
     * @param message Message to return the name of
     * @return Message name
     */
    template<typename MessageType>
    [[nodiscard]] constexpr std::string_view get_message_name(MessageType && message) noexcept
    {
        using message_type = std::decay_t<decltype(message)>;

        static_assert(is_ipc_message_type_v<message_type>, "MessageType must be an IPC type");
        return named_message_t<message_type>::name;
    }

    /**
     * Sent from agent->shell to tell when the agent is ready.
     */
    using msg_ready_t = message_t<message_key_t::ready, void, void>;
    DEFINE_NAMED_MESSAGE(msg_ready_t);

    /** Sent from shell->agent or vice-versa to tell it to shut down */
    using msg_shutdown_t = message_t<message_key_t::shutdown, void, void>;
    DEFINE_NAMED_MESSAGE(msg_shutdown_t);

    /** Sent from shell->agent carrying the monotonic_raw and monotonic start value and indicating that capture should start */
    using msg_start_t = message_t<message_key_t::start, monotonic_pair_t, void>;
    DEFINE_NAMED_MESSAGE(msg_start_t);

    /** Sent from the shell to all agents notifying them of the monitored PIDs */
    using msg_monitored_pids_t = message_t<message_key_t::monitored_pids, void, std::vector<pid_t>>;
    DEFINE_NAMED_MESSAGE(msg_monitored_pids_t);

    /** Sent from the annotation agent to the shell when a new annotation connection is received */
    using msg_annotation_new_conn_t = message_t<message_key_t::annotation_new_conn, annotation_uid_t, void>;
    DEFINE_NAMED_MESSAGE(msg_annotation_new_conn_t);

    /** Sent by the agent or shell to close a connection */
    using msg_annotation_close_conn_t = message_t<message_key_t::annotation_close_conn, annotation_uid_t, void>;
    DEFINE_NAMED_MESSAGE(msg_annotation_close_conn_t);

    /** Sent from the annotation agent to the shell when some data is received from an annotations connection */
    using msg_annotation_recv_bytes_t =
        message_t<message_key_t::annotation_recv_bytes, annotation_uid_t, std::vector<uint8_t>>;
    DEFINE_NAMED_MESSAGE(msg_annotation_recv_bytes_t);

    /** Sent from the shell to the annotation agent when some data is to be sent to the annotation connection */
    using msg_annotation_send_bytes_t =
        message_t<message_key_t::annotation_send_bytes, annotation_uid_t, std::vector<uint8_t>>;
    DEFINE_NAMED_MESSAGE(msg_annotation_send_bytes_t);

    /** Sent from shell to perfetto agent to create a new connection */
    using msg_perfetto_new_conn_t = message_t<message_key_t::perfetto_new_conn, void, void>;
    DEFINE_NAMED_MESSAGE(msg_perfetto_new_conn_t);

    /** Sent from shell to perfetto agent to close a connection */
    using msg_perfetto_close_conn_t = message_t<message_key_t::perfetto_close_conn, void, void>;
    DEFINE_NAMED_MESSAGE(msg_perfetto_close_conn_t);

    /** Sent from the Perfetto agent to the shell when some data is received from the Perfetto connection */
    using msg_perfetto_recv_bytes_t = message_t<message_key_t::perfetto_recv_bytes, void, std::vector<uint8_t>>;
    DEFINE_NAMED_MESSAGE(msg_perfetto_recv_bytes_t);

    /** Sent by the shell to configure the perf capture */
    using msg_capture_configuration_t =
        message_t<message_key_t::perf_capture_configuration, void, proto::shell::perf::capture_configuration_t>;
    DEFINE_NAMED_MESSAGE(msg_capture_configuration_t);

    /**
     * Sent by the perf agent when the prepare step is ready and the agent is able to start.
     * Contains the list of polled or forked child processes.
     */
    using msg_capture_ready_t = message_t<message_key_t::capture_ready, void, std::vector<pid_t>>;
    DEFINE_NAMED_MESSAGE(msg_capture_ready_t);

    /** Raw APC frame data sent by the perf agent.
     *
     * The APC data must not have the response type or length header fields,
     * these will be added by the receiver.
     */
    using msg_apc_frame_data_t = message_t<message_key_t::apc_frame_data, void, std::vector<uint8_t>>;
    DEFINE_NAMED_MESSAGE(msg_apc_frame_data_t);

    // this version is R/O send only object allowing send from span owned externally
    using msg_apc_frame_data_from_span_t = message_t<message_key_t::apc_frame_data, void, lib::Span<uint8_t const>>;
    DEFINE_NAMED_MESSAGE(msg_apc_frame_data_from_span_t);

    /** Sent by the perf agent to the shell once it is ready to capture the newly exec-d process */
    using msg_exec_target_app_t = message_t<message_key_t::exec_target_app, void, void>;
    DEFINE_NAMED_MESSAGE(msg_exec_target_app_t);

    /** Sent from perf agent to the shell one it detects a core online/offline state change */
    using msg_cpu_state_change_t = message_t<message_key_t::cpu_state_change, cpu_state_change_t, void>;
    DEFINE_NAMED_MESSAGE(msg_cpu_state_change_t);

    /** Sent from perf agent to shell if capture fails for some reason */
    using msg_capture_failed_t = message_t<message_key_t::capture_failed, capture_failed_reason_t, void>;
    DEFINE_NAMED_MESSAGE(msg_capture_failed_t);

    /** Sent from perf agent to shell starts capturing data */
    using msg_capture_started_t = message_t<message_key_t::capture_started, void, void>;
    DEFINE_NAMED_MESSAGE(msg_capture_started_t);

    /** All supported message types */
    using all_message_types_variant_t = std::variant<msg_ready_t,
                                                     msg_shutdown_t,
                                                     msg_start_t,
                                                     msg_monitored_pids_t,
                                                     msg_annotation_new_conn_t,
                                                     msg_annotation_close_conn_t,
                                                     msg_annotation_recv_bytes_t,
                                                     msg_annotation_send_bytes_t,
                                                     msg_perfetto_new_conn_t,
                                                     msg_perfetto_close_conn_t,
                                                     msg_perfetto_recv_bytes_t,
                                                     msg_capture_configuration_t,
                                                     msg_capture_ready_t,
                                                     msg_apc_frame_data_t,
                                                     msg_exec_target_app_t,
                                                     msg_cpu_state_change_t,
                                                     msg_capture_failed_t,
                                                     msg_capture_started_t>;
}
