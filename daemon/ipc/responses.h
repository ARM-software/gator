/* Copyright (C) 2022 by Arm Limited. All rights reserved. */
#pragma once

#include "ISender.h"
#include "lib/Span.h"

#include <vector>

namespace ipc {

    enum class response_type : char {
        unknown = 0,
        // Actual values understood by streamline
        xml = 1,
        apc_data = 3,
        ack = 4,
        nak = 5,
        current_config = 6,
        error = '\xFF'
    };

    template<response_type Key, typename Payload>
    struct response_t {
        static constexpr response_type key = Key;
        using payload_type = Payload;

        payload_type payload;
    };

    using response_apc_data_t = response_t<response_type::apc_data, std::vector<char>>;
    using response_xml_t = response_t<response_type::xml, std::vector<char>>;
    using response_current_config_t = response_t<response_type::current_config, std::vector<char>>;
    using response_error_t = response_t<response_type::error, std::vector<char>>;
    using response_ack_t = response_t<response_type::ack, std::vector<char>>;
    using response_nak_t = response_t<response_type::nak, std::vector<char>>;

    /** Traits object for response types */
    template<typename RepsonseType>
    struct repsonse_traits_t {
        static constexpr RepsonseType key = RepsonseType::RAW;
    };

    /** Traits object for response types derived from response_t */
    template<response_type Key, typename Payload>
    struct repsonse_traits_t<response_t<Key, Payload>> {
        using message_type = response_t<Key, Payload>;
        using payload_type = typename message_type::payload_type;

        static constexpr response_type key = message_type::key;
    };

    /** Helper trait to validate some response type */
    template<typename Response_type>
    static constexpr bool is_response_message_type_v = (repsonse_traits_t<Response_type>::key
                                                        != response_type::unknown);
}
