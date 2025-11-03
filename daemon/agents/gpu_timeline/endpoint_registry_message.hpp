/* Copyright (C) 2025 by Arm Limited. All rights reserved. */

#pragma once

#include "agents/gpu_timeline/message_header.hpp"
#include "lib/Span.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace agents::gpu_timeline {

/**
 * @brief Message representing the list of services which Gator is prepared to
 * provide
 */
class endpoint_registry_message_t {
public:
    /**
     * @brief Make an "endpoint registry" message from the passed message
     * header and the given list of endpoints. This is always a response to a
     * matching request message (see preconditions below).
     * @param message_pair_id Identity of the request to which this is a response
     */
    endpoint_registry_message_t(uint64_t message_pair_id);

    /**
     * @return Ordered list of spans representing this message: a serialized
     * header PLUS a serialized endpoint registry. This is a reference into
     * storage controlled by the present object. As such, the return value has
     * the lifetime of this endpoint_registry_message_t.
     */
    [[nodiscard]] std::vector<lib::Span<const char>> get_serialized() const;

    /**
     * @brief Components returned by get_serialized()
     */
    enum serialized_components_t : std::uint8_t {
        HEADER = 0,
        BODY = 1,
    };

private:
    uint64_t message_pair_id;

    /**
     * @brief Message header for response
     * Don't access directly - use get_serialized()
     */
    mutable std::optional<message_header_t> response_header;

    /**
     * @return Character-based representation of this message payload
     */
    [[nodiscard]] lib::Span<const char> get_serialized_endpoints() const;
};

} // namespace agents::gpu_timeline
