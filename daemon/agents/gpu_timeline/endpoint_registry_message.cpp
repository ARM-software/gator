/* Copyright (C) 2025 by Arm Limited. All rights reserved. */

#include "endpoint_registry_message.hpp"

#include <array>

using namespace agents::gpu_timeline;

namespace {
    const std::array<char, 29> STANDARD_ENDPOINT_REGISTRY {
        // endpoint 0 ID
        static_cast<char>(message_header_t::endpoint_t::LIST_ENDPOINTS),
        // endpoint 0 name length as LE-u32
        8,0,0,0,
        // endpoint 0 name string
        'r','e','g','i','s','t','r','y',
        // endpoint 1 id
        static_cast<char>(message_header_t::endpoint_t::TIMELINE),
        // endpoint 1 name length as LE-u32
        11,0,0,0,
        // endpoint 1 name string
        'G','P','U','T','i','m','e','l','i','n','e'
    };
}

endpoint_registry_message_t::endpoint_registry_message_t(uint64_t message_pair_id)
    : message_pair_id(message_pair_id)
{
}

lib::Span<const char> endpoint_registry_message_t::get_serialized_endpoints() const
{
    return STANDARD_ENDPOINT_REGISTRY;
}

std::vector<lib::Span<const char>> endpoint_registry_message_t::get_serialized() const
{
    lib::Span<const char> payload = get_serialized_endpoints();

    if (!response_header.has_value()) {
        response_header = {message_header_t::message_type_t::TX_RX,
                           message_header_t::endpoint_t::LIST_ENDPOINTS,
                           message_pair_id,
                           static_cast<uint32_t>(payload.size())};
    }

    return {response_header->get_serialized(), payload};
}
