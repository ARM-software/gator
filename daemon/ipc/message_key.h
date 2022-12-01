/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "lib/Span.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace ipc {
    /** Enumerates all known message types */
    enum class message_key_t : std::uint8_t {
        unknown = 0,

        // general
        ready,
        shutdown,
        start,
        monitored_pids,

        // external annotations
        annotation_new_conn,
        annotation_recv_bytes,
        annotation_send_bytes,
        annotation_close_conn,

        //Perfetto
        perfetto_new_conn,
        perfetto_recv_bytes,
        perfetto_send_bytes,
        perfetto_close_conn,

        // perf
        perf_capture_configuration,
        capture_ready,
        apc_frame_data,
        exec_target_app,
        cpu_state_change,
        capture_failed,
        capture_started,
    };

    /** The wire-size of the message key */
    static constexpr std::size_t message_key_size = sizeof(message_key_t);

    /** Copy the message key value into some buffer */
    inline lib::Span<char> copy_key_to_buffer(message_key_t key, lib::Span<char> buffer)
    {
        std::memcpy(buffer.data(), &key, sizeof(key));
        return buffer.subspan(sizeof(key));
    }
}
