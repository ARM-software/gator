/* Copyright (C) 2025 by Arm Limited. All rights reserved. */

#include "agents/gpu_timeline/message_header.hpp"

#include <cassert>

using namespace agents::gpu_timeline;

/**
 * @brief Message pair ID to send if a message DOES NOT require a response (if
 * it DOES NOT use message_type_t::TX_RX).
 *
 * All network messages contain a message pair ID field. In cases where a
 * message is sent requiring a response (message type TX_RX), a common value is
 * used in this field across request and response. This connects the two
 * messages. Message types TX and TX_ASYNC do not require responses. In those
 * cases, the arbitrary message pair ID below may be used.
 */
static constexpr uint64_t DUMMY_MESSAGE_PAIR_ID = 0;

message_header_t::message_header_t(lib::Span<const char> serialized_message)
    : raw_header(*reinterpret_cast<const raw_message_header_t *>(serialized_message.data()))
{
    assert(serialized_message.size() >= SERIALIZED_LENGTH);
};

message_header_t::message_header_t(message_type_t message_type,
                                   endpoint_t endpoint,
                                   std::optional<uint64_t> message_pair_id,
                                   uint32_t payload_size)
    : raw_header {message_type, endpoint, message_pair_id.value_or(DUMMY_MESSAGE_PAIR_ID), payload_size}
{
    assert((message_type == message_type_t::TX_RX && message_pair_id.has_value())
           || (message_type != message_type_t::STOP && !message_pair_id.has_value()));
}

lib::Span<const char> message_header_t::get_serialized() const
{
    return {reinterpret_cast<const char *>(&raw_header), sizeof(raw_header)};
}

message_header_t::endpoint_t message_header_t::get_endpoint() const
{
    return raw_header.endpoint;
}

std::optional<uint64_t> message_header_t::get_message_pair_id() const
{
    if (raw_header.message_type == message_type_t::TX_RX) {
        // On x86-64 specifically (not aarch64), directly returning
        // raw_header.message_pair_id (an unaligned value) causes an unaligned
        // pointer to be used. This then triggers a warning. Since there are
        // no explicit pointers here, this is presumably an implementation
        // detail which the compiler should resolve itself without a message.
        // We bypass the issue by assigning an intemediate constant.
        const uint64_t MESSAGE_PAIR_ID = raw_header.message_pair_id;
        return MESSAGE_PAIR_ID;
    }

    return std::nullopt;
}

uint32_t message_header_t::get_payload_size() const
{
    return raw_header.payload_size;
}
