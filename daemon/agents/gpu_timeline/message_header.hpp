/* Copyright (C) 2025 by Arm Limited. All rights reserved. */

#pragma once

#include <cstdint>
#include <optional>

#include <lib/Span.h>

namespace agents::gpu_timeline {

    /**
     * @brief Message header used in the GPU Timeline protocol. The remote side
     * of this protocol is usually implemented by a layer driver loaded into a
     * user's application.
     */
    class message_header_t {
    public:
        /**
         * @brief Service identifiers (endpoints).
         *
         * Strictly speaking, other than LIST_ENDPOINTS, these IDs are not
         * defined by the protocol and could vary. However, Gator can regard
         * these IDs as fixed since Gator defines them (here).
         */
        enum class endpoint_t : uint8_t {
            LIST_ENDPOINTS = 0,
            TIMELINE = 1,
        };

        /**
         * @brief Role of this message in the GPU Timeline protocol.
         */
        enum class message_type_t : uint8_t {
            TX_ASYNC = 0, ///< Transmit message (semantically identical to TX)
            TX = 1,       ///< Transmit message (semantically identical to TX_ASYNC)
            TX_RX = 2,    ///< EITHER a transmit message expecting a reply OR a reply to a TX_RX message
            STOP = 255,   ///< Reserved; DO NOT USE
        };

    private:
        /**
         * @brief Fixed-length header sent at start of all network messages
         * @note This packed layout allows for simple byte-for-byte copies
         * from the network messages we receive. However, since the protocol is
         * little-endian, our approach assumes that Gator is running on a
         * little-endian CPU (this is currently always the case).
         * @note ADAPTED FROM source_common/comms/comms_message.hpp, in the
         * libGPUlayers repository.
         */
        struct [[gnu::packed]] raw_message_header_t {
            message_type_t message_type;
            endpoint_t endpoint;
            uint64_t message_pair_id; ///< Unique message ID for message_type_t::TX_RX.
                                      ///< Needs to match in the request and the response.
            uint32_t payload_size;    ///< Message bytes remaining, following the header.
        };

        raw_message_header_t raw_header;

    public:
        /**
         * @brief Number of bytes in message header in serialized format, as
         * sent over network
         */
        static constexpr size_t SERIALIZED_LENGTH = sizeof(raw_message_header_t);

        /**
         * @brief Initialize the header from its serialized form. Only the
         * first SERIALIZED_LENGTH bytes of serialized_message are considered.
         * @param serialized_message Message in byte form
         * @pre serialized_message.size_bytes() >= SERIALIZED_LENGTH
         */
        message_header_t(lib::Span<const char> serialized_message);

        /**
         * @brief Initialize a message header from its component fields
         * @param message_type See message_type_t
         * @param endpoint See endpoint_t
         * @param message_pair_id Unique request/response ID. ONLY for
         * message_type == message_type_t::TX_RX; in this case, this parameter
         * MUST be supplied.
         * @param payload_size Number of bytes after the header (note that the
         * present class ONLY models the header). May be zero.
         */
        message_header_t(message_type_t message_type,
                         endpoint_t endpoint,
                         std::optional<uint64_t> message_pair_id,
                         uint32_t payload_size);

        /**
         * @brief Header in serialized format, as sent over network
         */
        [[nodiscard]] lib::Span<const char> get_serialized() const;

        /**
         * @return Identity of service to handle this message
         */
        [[nodiscard]] endpoint_t get_endpoint() const;

        /**
         * @return The shared identifier of this specific request/response pair
         * of messages, IF AND ONLY IF this message is part of a
         * request/response pair (either the request or the response).
         * Otherwise, std::nullopt.
         * @note Use this method to find if a message is part of a pair.
         */
        [[nodiscard]] std::optional<uint64_t> get_message_pair_id() const;

        /**
         * @return Number of bytes following the header (the "payload").
         * @note The return value may be zero (and often is).
         * @note This class provides NO mechanism to access the payload.
         */
        [[nodiscard]] uint32_t get_payload_size() const;
    };

} // namespace agents::gpu_timeline
