/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "ipc/message_key.h"
#include "lib/Span.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include <boost/asio/buffer.hpp>

#include <google/protobuf/message_lite.h>

namespace ipc {
    /** Check if the message header type is valid fixed size type */
    template<typename T>
    static constexpr bool is_valid_message_header_v =
        std::is_void_v<T>        //
        || std::is_pod_v<T>      //
        || std::is_integral_v<T> //
        || std::is_enum_v<T>     //
        || (std::is_array_v<T> && is_valid_message_header_v<std::remove_all_extents_t<T>>);

    /** True if @a T is a Protobuf message. */
    template<typename T>
    constexpr bool is_protobuf_message_v = std::is_base_of_v<google::protobuf::MessageLite, T>;

    /** Helper for testing equality of pb messages (since we cannot use MessageDifferencer with MessageLite). 
     * This method serializes the message and then compares the strings. It is primarily intended for unit testing. */
    template<typename T>
    constexpr bool same_pb_message(T const & a, T const & b)
    {
        return (a.SerializeAsString() == b.SerializeAsString());
    }

    /** Basic message type */
    template<message_key_t Key, typename HeaderType, typename SuffixType>
    struct message_t {
        static_assert(Key != message_key_t::unknown);
        static_assert(is_valid_message_header_v<HeaderType>);

        using header_type = HeaderType;
        using suffix_type = SuffixType;

        static constexpr message_key_t key = Key;

        header_type header;
        suffix_type suffix;

        friend constexpr bool operator==(message_t const & a, message_t const & b)
        {
            const auto header_match = a.header == b.header;
            if constexpr (is_protobuf_message_v<suffix_type>) {
                return header_match && same_pb_message(a.suffix, b.suffix);
            }
            else {
                return header_match && (a.suffix == b.suffix);
            }
        }

        friend constexpr bool operator!=(message_t const & a, message_t const & b) { return !(a == b); }
    };

    /** Basic message type where the key is the only part of the message */
    template<message_key_t Key>
    struct message_t<Key, void, void> {
        static_assert(Key != message_key_t::unknown);

        using header_type = void;
        using suffix_type = void;

        static constexpr message_key_t key = Key;

        friend constexpr bool operator==(message_t const & /*a*/, message_t const & /*b*/) { return true; }

        friend constexpr bool operator!=(message_t const & /*a*/, message_t const & /*b*/) { return false; }
    };

    /** Basic message type where the key & header are the only part of the message */
    template<message_key_t Key, typename HeaderType>
    struct message_t<Key, HeaderType, void> {
        static_assert(Key != message_key_t::unknown);
        static_assert(is_valid_message_header_v<HeaderType>);

        using header_type = HeaderType;
        using suffix_type = void;

        static constexpr message_key_t key = Key;

        header_type header;

        friend constexpr bool operator==(message_t const & a, message_t const & b) { return (a.header == b.header); }

        friend constexpr bool operator!=(message_t const & a, message_t const & b) { return !(a == b); }
    };

    /** Basic message type where the key & suffix are the only part of the message */
    template<message_key_t Key, typename SuffixType>
    struct message_t<Key, void, SuffixType> {
        static_assert(Key != message_key_t::unknown);

        using header_type = void;
        using suffix_type = SuffixType;

        static constexpr message_key_t key = Key;

        suffix_type suffix;

        friend constexpr bool operator==(message_t const & a, message_t const & b)
        {
            if constexpr (is_protobuf_message_v<suffix_type>) {
                return same_pb_message(a.suffix, b.suffix);
            }
            else {
                return a.suffix == b.suffix;
            }
        }

        friend constexpr bool operator!=(message_t const & a, message_t const & b) { return !(a == b); }
    };

    /** Traits object for message types */
    template<typename MessageType>
    struct message_traits_t {
        static constexpr message_key_t key = message_key_t::unknown;
    };

    /** Traits object for message types derived from message_t */
    template<message_key_t Key, typename HeaderType, typename SuffixType>
    struct message_traits_t<message_t<Key, HeaderType, SuffixType>> {
        using message_type = message_t<Key, HeaderType, SuffixType>;
        using header_type = typename message_type::header_type;
        using suffix_type = typename message_type::suffix_type;

        static constexpr message_key_t key = message_type::key;
    };

    /** Helper trait to validate some message type */
    template<typename MessageType>
    static constexpr bool is_ipc_message_type_v = (message_traits_t<MessageType>::key != message_key_t::unknown);
}
