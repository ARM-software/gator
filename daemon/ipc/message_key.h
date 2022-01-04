/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

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
        shutdown,

        // external annotations
        annotation_new_conn,
        annotation_recv_bytes,
        annotation_send_bytes,
        annotation_close_conn,
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
