/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

/**
 * Encode/Decode related functions for preparing IPC messages for transmit/receive.
 *
 * The codec is designed to try to limit the amount of transformation and allocation required.
 * The most simple messages can be identified by the single 'key' field (which identifies the message type).
 * Slightly more complete messages may have a fixed header, which *must* be a simple plain data type (POD, integer, array their-of)
 * which can be memcpy-ed or read directly into the pipe.
 * More complex message have a variable length blob, with a length-prefix. The codec is designed so that either pre-allocated buffers
 * can be transmitted for this part of the message (i.e. sending the byte data as-is), or where necessary a conversion/encoding
 * operation can be supported.
 *
 * Each IPC message is of the form `[key] ([header])? ([length] [suffix])?`, where:
 *  - [key] is the unique message identifier that identifies the message type. It is always present.
 *  - [header] is a structure who's size is fixed for a given value of [key], but may differ between different [key]'s.
 *             It contains simple fixed data always associated with that message type. It may be zero length for a given
 *             message type.
 *  - [length] is the length of the [suffix] region, given as a std::size_t. If the [suffix] region is not required by
 *             the message type, then [length] will also not be present.
 *  - [suffix] is the variable length data blob associated with some message. For a given [key] can vary in length from
 *             message to message. May not be present for all message types.
 */

#pragma once

#include "ipc/message_key.h"
#include "ipc/message_traits.h"
#include "ipc/responses.h"
#include "lib/Assert.h"
#include "lib/Span.h"

#include <cstdlib>
#include <cstring>
#include <type_traits>

#include <boost/asio/buffer.hpp>
#include <boost/system/errc.hpp>

#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

namespace ipc {

    /** Helper for simple data types that can be blitted directly from memory into the message without additonal encoding (such as strings, arrays of pods etc) */
    template<typename T, typename U>
    struct byte_span_blob_codec_t {
        /** The blob type */
        using value_type = T;

        /** The scatter-gather helper object which stores the length and buffer so that the length field may be scatter-gathered */
        struct sg_write_helper_type {
            char const * data = nullptr;
            std::size_t length = 0;
        };

        /** The scatter-gather helper object used for reading the suffix */
        struct sg_read_helper_type {
            std::size_t length = 0;
        };

        /** The number of buffers required to perform a scatter gather based write of the length + suffix fields */
        static constexpr std::size_t sg_writer_buffers_count = 2;

        /** The size of the length field */
        static constexpr std::size_t length_size = sizeof(U);

        /** Fill the sg_write_helper_type value */
        static constexpr sg_write_helper_type fill_sg_write_helper_type(value_type const & buffer)
        {
            using member_type = std::decay_t<decltype(*buffer.data())>;
            static_assert(
                (std::is_array_v<member_type> && is_valid_message_header_v<std::remove_all_extents_t<member_type>>)
                || std::is_pod_v<member_type> || std::is_integral_v<member_type> || std::is_enum_v<member_type>);

            return {reinterpret_cast<char const *>(buffer.data()), buffer.size() * sizeof(member_type)};
        }

        /** The total size required to store the encoded suffix buffer + length field */
        static constexpr std::size_t suffix_write_size(sg_write_helper_type const & helper)
        {
            return length_size + helper.length;
        }

        /** Fill a scatter-gather buffer list for writing out the header */
        static constexpr void fill_sg_buffer(lib::Span<boost::asio::const_buffer> sg_list,
                                             sg_write_helper_type const & helper)
        {
            sg_list[0] = {reinterpret_cast<char const *>(&helper.length), length_size};
            sg_list[1] = {helper.data, helper.length};
        }

        /** Read the suffix length from some byte span. */
        static constexpr lib::Span<char const> read_suffix_length(lib::Span<char const> const & bytes,
                                                                  std::size_t & length)
        {
            runtime_assert(bytes.size() >= length_size, "caller must ensure bytes is big enough for length field");

            std::memcpy(&length, bytes.data(), length_size);

            return bytes.subspan(length_size);
        }

        /** Read the suffix value from bytes (which must be set to the length given by read_suffix_length) */
        static constexpr void read_suffix(lib::Span<char const> const & bytes, value_type & buffer)
        {
            using member_type = std::decay_t<decltype(*buffer.data())>;
            static_assert(
                (std::is_array_v<member_type> && is_valid_message_header_v<std::remove_all_extents_t<member_type>>)
                || std::is_pod_v<member_type> || std::is_integral_v<member_type> || std::is_enum_v<member_type>);

            auto n = bytes.size() / sizeof(member_type);

            runtime_assert((n * sizeof(member_type)) == bytes.size(), "Unexpected suffix length");

            buffer.resize(n);

            std::memcpy(buffer.data(), bytes.data(), bytes.size());
        }

        /** Decode the suffix buffer data into the message */
        static constexpr auto read_suffix(sg_read_helper_type const & /*helper*/, value_type & /*buffer*/)
        {
            // nothing to do; handled by reading fully into mutable_suffix_buffer
            return boost::system::error_code {};
        }

        /** Return a mutable buffer to store the length of the suffix for some read operation via scatter-gather read */
        static auto mutable_length_buffer(sg_read_helper_type & helper)
        {
            return boost::asio::mutable_buffer {reinterpret_cast<char *>(&helper.length), length_size};
        }

        /** Return a mutable buffer to store the suffix for some read operation via scatter-gather read */
        static auto mutable_suffix_buffer(value_type & buffer, sg_read_helper_type & helper)
        {
            using member_type = std::decay_t<decltype(*buffer.data())>;
            static_assert(
                (std::is_array_v<member_type> && is_valid_message_header_v<std::remove_all_extents_t<member_type>>)
                || std::is_pod_v<member_type> || std::is_integral_v<member_type> || std::is_enum_v<member_type>);

            auto n = helper.length / sizeof(member_type);

            runtime_assert((n * sizeof(member_type)) == helper.length, "Unexpected suffix length");

            buffer.resize(n);

            return boost::asio::mutable_buffer {reinterpret_cast<char *>(buffer.data()), helper.length};
        }
    };

    /**
     * Blob codec provides mechanism for encoding/decoding some suffix_type into a byte sequence.
     * It can be specialized for cases where the suffix must be first encoded into some temporary buffer, or for when
     * it can be blitted directly from memory.
     */
    template<typename T, typename U, typename Enable = void>
    struct blob_codec_t;

    template<typename U>
    struct blob_codec_t<void, U> {
        /** The blob type */
        using value_type = void;

        /** The scatter-gather helper object which stores the length and buffer so that the length field may be scatter-gathered */
        struct sg_write_helper_type {
            // no fields
        };

        /** The scatter-gather helper object used for reading the suffix */
        struct sg_read_helper_type {
        };

        /** The number of buffers required to perform a scatter gather based write of the length + suffix fields (in this case nothing) */
        static constexpr std::size_t sg_writer_buffers_count = 0;

        /** The size of the length field */
        static constexpr std::size_t length_size = 0;

        /** Fill the sg_write_helper_type value */
        static constexpr sg_write_helper_type fill_sg_write_helper_type() { return {}; }

        /** The total size required to store the encoded suffix buffer + length field */
        static constexpr std::size_t suffix_write_size(sg_write_helper_type const & /*helper*/) { return 0; }

        /** Fill a scatter-gather buffer list for writing out the header */
        static constexpr void fill_sg_buffer(lib::Span<boost::asio::const_buffer> /*sg_list*/,
                                             sg_write_helper_type const & /*helper*/)
        {
        }

        /** Read the suffix length from some byte span. */
        static constexpr lib::Span<char const> read_suffix_length(lib::Span<char const> const & bytes,
                                                                  std::size_t & length)
        {
            length = 0;
            return bytes;
        }

        /** Decode the suffix buffer data into the message */
        static constexpr auto read_suffix(sg_read_helper_type const & /*helper*/)
        {
            // nothing to do
            return boost::system::error_code {};
        }

        /** Return a mutable buffer to store the length of the suffix for some read operation via scatter-gather read */
        static auto mutable_length_buffer(sg_read_helper_type & /*helper*/) { return boost::asio::mutable_buffer(); }

        /** Return a mutable buffer to store the suffix for some read operation via scatter-gather read */
        static auto mutable_suffix_buffer(sg_read_helper_type & /*helper*/) { return boost::asio::mutable_buffer(); }
    };

    /** Specialization for vector of integrals */
    template<typename T, typename U>
    struct blob_codec_t<std::vector<T>, U, std::enable_if_t<std::is_integral_v<T>>>
        : byte_span_blob_codec_t<std::vector<T>, U> {
    };

    /** Specialization for Span of integrals */
    template<typename T, typename U>
    struct blob_codec_t<lib::Span<T const>, U, std::enable_if_t<std::is_integral_v<T>>>
        : byte_span_blob_codec_t<lib::Span<T const>, U> {
    };

    /** Specialization for protobuf messages */
    template<typename T, typename U>
    struct blob_codec_t<T, U, std::enable_if_t<is_protobuf_message_v<T>>> {
        using value_type = T;

        // Unlike byte_span_blob_codec_t, the protobuf classes cannot be their
        // own buffers
        struct sg_write_helper_type {
            std::size_t length = 0;
            std::string data;
        };

        struct sg_read_helper_type {
            std::size_t length = 0;
            std::vector<char> buffer;
        };

        static constexpr std::size_t sg_writer_buffers_count = 2;
        static constexpr std::size_t length_size = sizeof(std::size_t);

        static constexpr sg_write_helper_type fill_sg_write_helper_type(value_type const & pb)
        {
            auto helper = sg_write_helper_type {};
            pb.SerializeToString(&helper.data);
            helper.length = helper.data.size();
            return helper;
        }

        static constexpr std::size_t suffix_write_size(sg_write_helper_type const & helper)
        {
            return length_size + helper.length;
        }

        static constexpr void fill_sg_buffer(lib::Span<boost::asio::const_buffer> sg_list,
                                             sg_write_helper_type const & helper)
        {
            sg_list[0] = {reinterpret_cast<char const *>(&helper.length), length_size};
            sg_list[1] = {helper.data.data(), helper.length};
        }

        static constexpr lib::Span<char const> read_suffix_length(lib::Span<char const> const & bytes,
                                                                  std::size_t & length)
        {
            runtime_assert(bytes.size() >= length_size, "caller must ensure bytes is big enough for length field");

            std::memcpy(&length, bytes.data(), length_size);

            return bytes.subspan(length_size);
        }

        static auto read_suffix(sg_read_helper_type const & helper, value_type & pb)
        {
            auto stream = google::protobuf::io::ArrayInputStream(helper.buffer.data(), helper.buffer.size());

            if (pb.ParseFromZeroCopyStream(&stream)) {
                return boost::system::error_code {};
            }
            return boost::system::errc::make_error_code(boost::system::errc::protocol_error);
        }

        static auto mutable_length_buffer(sg_read_helper_type & helper)
        {
            return boost::asio::mutable_buffer {reinterpret_cast<char *>(&helper.length), length_size};
        }

        static auto mutable_suffix_buffer(value_type & /*buffer*/, sg_read_helper_type & helper)
        {
            helper.buffer.resize(helper.length);
            return boost::asio::mutable_buffer {reinterpret_cast<char *>(helper.buffer.data()), helper.length};
        }
    };
    template<typename MessageType>
    struct key_codec_key_type_t;
    // specialized for message_t
    template<message_key_t K, typename H, typename S>
    struct key_codec_key_type_t<message_t<K, H, S>> {
        using key_type = message_key_t;
    };
    // specialized for response_t
    template<response_type K, typename P>
    struct key_codec_key_type_t<response_t<K, P>> {
        using key_type = response_type;
    };

    /**
     * Codec object for the 'key' value
     *
     * This class provides the means to encode and decode just the `[key]` part of a message.
     */
    template<typename MessageType>
    struct key_codec_t {
        /** The message key type */
        using key_type = typename key_codec_key_type_t<MessageType>::key_type;
        /** The number of buffers required to perform a scatter gather based write of the key */
        static constexpr std::size_t sg_writer_buffers_count = 1;
        /** The total size required to encode/decode the key + header */
        static constexpr std::size_t key_size = sizeof(key_type);

        /** Fill a scatter-gather buffer list for writing out the key */
        static void fill_sg_buffer(lib::Span<boost::asio::const_buffer> sg_list, message_key_t const & key)
        {
            sg_list[0] = {reinterpret_cast<char const *>(&key), key_size};
        }

        /** Fill a scatter-gather buffer list for writing out the key */
        static void fill_sg_buffer(lib::Span<boost::asio::const_buffer> sg_list, response_type const & key)
        {
            sg_list[0] = {reinterpret_cast<char const *>(&key), key_size};
        }
        /** Make a mutable buffer out of the key (for reading into) */
        static auto mutable_buffer(message_key_t & key)
        {
            return boost::asio::mutable_buffer {reinterpret_cast<char *>(&key), key_size};
        }

        /** Read the key value from some byte-span. */
        static lib::Span<char const> read_key(lib::Span<char const> const & bytes, message_key_t & key)
        {
            runtime_assert(bytes.size() >= key_size, "caller must ensure bytes is big enough for key");

            std::memcpy(&key, bytes.data(), key_size);

            return bytes.subspan(key_size);
        }
    };

    /**
     * Class that provides the codec for the header part of a message.
     *
     * This class provides the means to encode and decode just the `([header])?` part of a message.
     *
     * @tparam MessageType The message type
     */
    template<typename MessageType>
    struct header_codec_t;

    /** Specialization for message types based on message_t where header_type is response_t which has no header */
    template<response_type Key, typename Payload>
    struct header_codec_t<response_t<Key, Payload>> {
        /** The message type */
        using message_type = response_t<Key, Payload>;

        /** The number of buffers required to perform a scatter gather based write of the header (which in this case 0 as there is no header) */
        static constexpr std::size_t sg_writer_buffers_count = 0;
        /** The total size required to encode/decode the key + header (which in this case is 0 as there is no header) */
        static constexpr std::size_t header_size = 0;

        //static_assert(Key != message_key_t::unknown);
        //static_assert(std::is_void_v<header_type>);

        /** Fill a scatter-gather buffer list for writing out the header */
        static constexpr void fill_sg_buffer(lib::Span<boost::asio::const_buffer> /*sg_list*/,
                                             message_type const & /*message*/)
        {
        }

        /** Make a mutable buffer out of the header (for reading into) */
        static auto mutable_buffer(message_type & /*message*/) { return boost::asio::mutable_buffer {}; }

        /** Read the header from some byte-span. */
        static constexpr lib::Span<char const> read_header(lib::Span<char const> const & bytes,
                                                           message_type & /*message*/)
        {
            return bytes;
        }
    };

    /** Specialization for message types based on message_t */
    template<message_key_t Key, typename HeaderType, typename SuffixType>
    struct header_codec_t<message_t<Key, HeaderType, SuffixType>> {
        /** The message type */
        using message_type = message_t<Key, HeaderType, SuffixType>;
        /** The header type */
        using header_type = typename message_type::header_type;
        /** The message key */
        static constexpr message_key_t key = message_type::key;
        /** The number of buffers required to perform a scatter gather based write of the  header */
        static constexpr std::size_t sg_writer_buffers_count = 1;
        /** The total size required to encode/decode the  header */
        static constexpr std::size_t header_size = sizeof(header_type);

        static_assert(Key != message_key_t::unknown);
        static_assert(!std::is_void_v<header_type>);
        static_assert(is_valid_message_header_v<HeaderType>);

        /** Fill a scatter-gather buffer list for writing out the header */
        static constexpr void fill_sg_buffer(lib::Span<boost::asio::const_buffer> sg_list, message_type const & message)
        {
            sg_list[0] = {reinterpret_cast<char const *>(&message.header), header_size};
        }

        /** Make a mutable buffer out of the header (for reading into) */
        static auto mutable_buffer(message_type & message)
        {
            return boost::asio::mutable_buffer {reinterpret_cast<char *>(&message.header), header_size};
        }

        /** Read the header from some byte-span. */
        static lib::Span<char const> read_header(lib::Span<char const> const & bytes, message_type & message)
        {
            runtime_assert(bytes.size() >= header_size, "caller must ensure bytes is big enough for header");

            std::memcpy(&message.header, bytes.data(), header_size);

            return bytes.subspan(header_size);
        }
    };

    /** Specialization for message types based on message_t where header_type is void (meaning not present) */
    template<message_key_t Key, typename SuffixType>
    struct header_codec_t<message_t<Key, void, SuffixType>> {
        /** The message type */
        using message_type = message_t<Key, void, SuffixType>;
        /** The header type */
        using header_type = typename message_type::header_type;
        /** The message key */
        static constexpr message_key_t key = message_type::key;
        /** The number of buffers required to perform a scatter gather based write of the header (which in this case 0 as there is no header) */
        static constexpr std::size_t sg_writer_buffers_count = 0;
        /** The total size required to encode/decode the key + header (which in this case is 0 as there is no header) */
        static constexpr std::size_t header_size = 0;

        static_assert(Key != message_key_t::unknown);
        static_assert(std::is_void_v<header_type>);

        /** Fill a scatter-gather buffer list for writing out the header */
        static constexpr void fill_sg_buffer(lib::Span<boost::asio::const_buffer> /*sg_list*/,
                                             message_type const & /*message*/)
        {
        }

        /** Make a mutable buffer out of the header (for reading into) */
        static auto mutable_buffer(message_type & /*message*/) { return boost::asio::mutable_buffer {}; }

        /** Read the header from some byte-span. */
        static constexpr lib::Span<char const> read_header(lib::Span<char const> const & bytes,
                                                           message_type & /*message*/)
        {
            return bytes;
        }
    };

    /** suffix_codec_t provides similar handling to header_codec_t, but for handling the variable length suffix part of the message */
    template<typename MessageType>
    struct suffix_codec_t;

    /** Specialization for message types based on response_t */
    template<response_type Key, typename Payload>
    struct suffix_codec_t<response_t<Key, Payload>> {
        /** The message type */
        using message_type = response_t<Key, Payload>;
        /** The suffix type */
        using suffix_type = typename message_type::payload_type;
        /** The encoder type */
        using encoder_type = blob_codec_t<suffix_type, std::int32_t>;
        /** The scatter-gather helper object which stores the length and buffer so that the length field may be scatter-gathered */
        using sg_write_helper_type = typename encoder_type::sg_write_helper_type;

        /** The number of buffers required to perform a scatter gather based write of the length + suffix fields */
        static constexpr std::size_t sg_writer_buffers_count = encoder_type::sg_writer_buffers_count;

        /** The size of the length field */
        static constexpr std::size_t length_size = sizeof(std::int32_t);

        /** Fill the sg_write_helper_type value */
        static constexpr sg_write_helper_type fill_sg_write_helper_type(message_type const & message)
        {
            return encoder_type::fill_sg_write_helper_type(message.payload);
        }

        /** The total size required to store the encoded suffix buffer + length field */
        static constexpr std::size_t suffix_write_size(sg_write_helper_type const & helper)
        {
            return encoder_type::suffix_write_size(helper);
        }

        /** Fill a scatter-gather buffer list for writing out the header */
        static constexpr void fill_sg_buffer(lib::Span<boost::asio::const_buffer> sg_list,
                                             sg_write_helper_type const & helper)
        {
            return encoder_type::fill_sg_buffer(sg_list, helper);
        }
    };

    /** Specialization for message types based on message_t */
    template<message_key_t Key, typename HeaderType, typename SuffixType>
    struct suffix_codec_t<message_t<Key, HeaderType, SuffixType>> {
        /** The message type */
        using message_type = message_t<Key, HeaderType, SuffixType>;
        /** The suffix type */
        using suffix_type = typename message_type::suffix_type;
        /** The encoder type */
        using encoder_type = blob_codec_t<suffix_type, std::size_t>;
        /** The scatter-gather helper object which stores the length and buffer so that the length field may be scatter-gathered */
        using sg_write_helper_type = typename encoder_type::sg_write_helper_type;
        /** The scatter-gather helper object used for reading the suffix */
        using sg_read_helper_type = typename encoder_type::sg_read_helper_type;

        static_assert(!std::is_void_v<suffix_type>);

        /** The number of buffers required to perform a scatter gather based write of the length + suffix fields */
        static constexpr std::size_t sg_writer_buffers_count = encoder_type::sg_writer_buffers_count;

        /** The size of the length field */
        static constexpr std::size_t length_size = encoder_type::length_size;

        /** Fill the sg_write_helper_type value */
        static constexpr sg_write_helper_type fill_sg_write_helper_type(message_type const & message)
        {
            return encoder_type::fill_sg_write_helper_type(message.suffix);
        }

        /** The total size required to store the encoded suffix buffer + length field */
        static constexpr std::size_t suffix_write_size(sg_write_helper_type const & helper)
        {
            return encoder_type::suffix_write_size(helper);
        }

        /** Fill a scatter-gather buffer list for writing out the header */
        static constexpr void fill_sg_buffer(lib::Span<boost::asio::const_buffer> sg_list,
                                             sg_write_helper_type const & helper)
        {
            return encoder_type::fill_sg_buffer(sg_list, helper);
        }

        /** Read the suffix length from some byte span. */
        static constexpr lib::Span<char const> read_suffix_length(lib::Span<char const> const & bytes,
                                                                  std::size_t & length)
        {
            return encoder_type::read_suffix_length(bytes, length);
        }

        /** Read the suffix value from bytes (which must be set to the length given by read_suffix_length) */
        static constexpr void read_suffix(lib::Span<char const> const & bytes, message_type & message)
        {
            encoder_type::read_suffix(bytes, message.suffix);
        }

        /** Decode the suffix buffer data into the message */
        static constexpr auto read_suffix(sg_read_helper_type const & helper, message_type & message)
        {
            return encoder_type::read_suffix(helper, message.suffix);
        }

        /** Return a mutable buffer to store the length of the suffix for some read operation via scatter-gather read */
        static constexpr auto mutable_length_buffer(sg_read_helper_type & helper)
        {
            return encoder_type::mutable_length_buffer(helper);
        }

        /** Return a mutable buffer to store the suffix for some read operation via scatter-gather read */
        static constexpr auto mutable_suffix_buffer(message_type & message, sg_read_helper_type & helper)
        {
            return encoder_type::mutable_suffix_buffer(message.suffix, helper);
        }
    };

    /** Specialization for message types based on message_t where suffix_type is void (meaning not present) */
    template<message_key_t Key, typename HeaderType>
    struct suffix_codec_t<message_t<Key, HeaderType, void>> {
        /** The message type */
        using message_type = message_t<Key, HeaderType, void>;
        /** The suffix type */
        using suffix_type = typename message_type::suffix_type;
        /** The encoder type */
        using encoder_type = blob_codec_t<suffix_type, std::size_t>;
        /** The scatter-gather helper object which stores the length and buffer so that the length field may be scatter-gathered */
        using sg_write_helper_type = typename encoder_type::sg_write_helper_type;
        /** The scatter-gather helper object used for reading the suffix */
        using sg_read_helper_type = typename encoder_type::sg_read_helper_type;

        static_assert(std::is_void_v<suffix_type>);

        /** The number of buffers required to perform a scatter gather based write of the length + suffix fields */
        static constexpr std::size_t sg_writer_buffers_count = encoder_type::sg_writer_buffers_count;

        /** The size of the length field */
        static constexpr std::size_t length_size = encoder_type::length_size;

        /** Fill the sg_write_helper_type value */
        static constexpr sg_write_helper_type fill_sg_write_helper_type(message_type const & /*message*/)
        {
            return encoder_type::fill_sg_write_helper_type();
        }

        /** The total size required to store the encoded suffix buffer + length field */
        static constexpr std::size_t suffix_write_size(sg_write_helper_type const & helper)
        {
            return encoder_type::suffix_write_size(helper);
        }

        /** Fill a scatter-gather buffer list for writing out the header */
        static constexpr void fill_sg_buffer(lib::Span<boost::asio::const_buffer> sg_list,
                                             sg_write_helper_type const & helper)
        {
            return encoder_type::fill_sg_buffer(sg_list, helper);
        }

        /** Read the suffix length from some byte span. */
        static constexpr lib::Span<char const> read_suffix_length(lib::Span<char const> const & bytes,
                                                                  std::size_t & length)
        {
            return encoder_type::read_suffix_length(bytes, length);
        }

        /** Read the suffix value from bytes (which must be set to the length given by read_suffix_length) */
        static constexpr void read_suffix(lib::Span<char const> const & bytes, message_type & /*message*/)
        {
            runtime_assert(bytes.empty(), "expected zero length suffix data");
        }

        /** Decode the suffix buffer data into the message */
        static constexpr auto read_suffix(sg_read_helper_type const & helper, message_type & /*message*/)
        {
            return encoder_type::read_suffix(helper);
        }

        /** Return a mutable buffer to store the length of the suffix for some read operation via scatter-gather read */
        static constexpr auto mutable_length_buffer(sg_read_helper_type & helper)
        {
            return encoder_type::mutable_length_buffer(helper);
        }

        /** Return a mutable buffer to store the suffix for some read operation via scatter-gather read */
        static constexpr auto mutable_suffix_buffer(message_type & /*message*/, sg_read_helper_type & helper)
        {
            return encoder_type::mutable_suffix_buffer(helper);
        }
    };
}
