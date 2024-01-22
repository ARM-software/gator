/* Copyright (C) 2019-2023 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_ARMNN_BYTE_ORDER_H
#define INCLUDE_ARMNN_BYTE_ORDER_H

#include "lib/Assert.h"
#include "lib/Span.h"

#include <cstdint>
#include <type_traits>

namespace armnn {
    /** Enumerate possible byte orderring */
    enum class ByteOrder : char { LITTLE, BIG };

#if (!defined(__BYTE_ORDER__)) || !(defined(__ORDER_BIG_ENDIAN__) || defined(__ORDER_LITTLE_ENDIAN__))
#error "Unable to determine byte order"
#endif

    namespace byte_order {
        template<typename T>
        inline constexpr bool is_byte_v = std::is_same_v<T, char>         //
                                       || std::is_same_v<T, std::uint8_t> //
                                       || std::is_same_v<T, std::int8_t>;

        template<typename T, typename = std::enable_if_t<is_byte_v<T>>>
        using ByteArray = lib::Span<const T>;

        /** Define the system byte order */
        static constexpr ByteOrder SYSTEM =
            (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) ? ByteOrder::LITTLE : ByteOrder::BIG;

        /** Read an unaligned 16-bit value from some byte array. */
        template<typename T>
        inline std::uint16_t get_16(ByteOrder order, ByteArray<T> data, std::size_t offset = 0)
        {
            runtime_assert((offset + 2) <= data.size(), "Invalid offset");

            if (order == ByteOrder::LITTLE) {
                return (std::uint16_t(data[offset + 0]) & 0xff) //
                     | ((std::uint16_t(data[offset + 1]) & 0xff) << 8);
            }
            return (std::uint16_t(data[offset + 1]) & 0xff) //
                 | ((std::uint16_t(data[offset + 0]) & 0xff) << 8);
        }

        /** Read an unaligned 32-bit value from some byte array. */
        template<typename T>
        inline std::uint32_t get_32(ByteOrder order, ByteArray<T> data, std::size_t offset = 0)
        {
            runtime_assert((offset + 4) <= data.size(), "Invalid offset");

            if (order == ByteOrder::LITTLE) {
                return (std::uint32_t(data[offset + 0]) & 0xff)         //
                     | ((std::uint32_t(data[offset + 1]) & 0xff) << 8)  //
                     | ((std::uint32_t(data[offset + 2]) & 0xff) << 16) //
                     | ((std::uint32_t(data[offset + 3]) & 0xff) << 24);
            }
            return (std::uint32_t(data[offset + 3]) & 0xff)         //
                 | ((std::uint32_t(data[offset + 2]) & 0xff) << 8)  //
                 | ((std::uint32_t(data[offset + 1]) & 0xff) << 16) //
                 | ((std::uint32_t(data[offset + 0]) & 0xff) << 24);
        }

        /** Read an unaligned 64-bit value from some byte array. */
        template<typename T>
        inline std::uint64_t get_64(ByteOrder order, ByteArray<T> data, std::size_t offset = 0)
        {
            runtime_assert((offset + 8) <= data.size(), "Invalid offset");

            if (order == ByteOrder::LITTLE) {
                return (std::uint64_t(data[offset + 0]) & 0xff)         //
                     | ((std::uint64_t(data[offset + 1]) & 0xff) << 8)  //
                     | ((std::uint64_t(data[offset + 2]) & 0xff) << 16) //
                     | ((std::uint64_t(data[offset + 3]) & 0xff) << 24) //
                     | ((std::uint64_t(data[offset + 4]) & 0xff) << 32) //
                     | ((std::uint64_t(data[offset + 5]) & 0xff) << 40) //
                     | ((std::uint64_t(data[offset + 6]) & 0xff) << 48) //
                     | ((std::uint64_t(data[offset + 7]) & 0xff) << 56);
            }
            return (std::uint64_t(data[offset + 7]) & 0xff)         //
                 | ((std::uint64_t(data[offset + 6]) & 0xff) << 8)  //
                 | ((std::uint64_t(data[offset + 5]) & 0xff) << 16) //
                 | ((std::uint64_t(data[offset + 4]) & 0xff) << 24) //
                 | ((std::uint64_t(data[offset + 3]) & 0xff) << 32) //
                 | ((std::uint64_t(data[offset + 2]) & 0xff) << 40) //
                 | ((std::uint64_t(data[offset + 1]) & 0xff) << 48) //
                 | ((std::uint64_t(data[offset + 0]) & 0xff) << 56);
        }
    }
}

#endif // INCLUDE_ARMNN_BYTE_ORDER_H
