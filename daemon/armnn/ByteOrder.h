/* Copyright (C) 2019-2021 by Arm Limited. All rights reserved. */

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
        using is_byte = std::integral_constant<bool,
                                               std::is_same<T, char>::value                //
                                                   || std::is_same<T, std::uint8_t>::value //
                                                   || std::is_same<T, std::int8_t>::value>;

        template<typename T, typename = typename std::enable_if<is_byte<T>::value>::type>
        using ByteArray = lib::Span<const T>;

        /** Define the system byte order */
        static constexpr ByteOrder SYSTEM =
            (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) ? ByteOrder::LITTLE : ByteOrder::BIG;

        /** Read an unaligned 16-bit value from some byte array. */
        template<typename T>
        inline std::uint16_t get_16(ByteOrder order, ByteArray<T> data, std::size_t offset = 0)
        {
            runtime_assert((offset + 2) <= data.length, "Invalid offset");

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
            runtime_assert((offset + 4) <= data.length, "Invalid offset");

            if (order == ByteOrder::LITTLE) {
                return (std::uint32_t(data[offset + 0]) & 0xff)           //
                       | ((std::uint32_t(data[offset + 1]) & 0xff) << 8)  //
                       | ((std::uint32_t(data[offset + 2]) & 0xff) << 16) //
                       | ((std::uint32_t(data[offset + 3]) & 0xff) << 24);
            }
            return (std::uint32_t(data[offset + 3]) & 0xff)           //
                   | ((std::uint32_t(data[offset + 2]) & 0xff) << 8)  //
                   | ((std::uint32_t(data[offset + 1]) & 0xff) << 16) //
                   | ((std::uint32_t(data[offset + 0]) & 0xff) << 24);
        }

        /** Read an unaligned 64-bit value from some byte array. */
        template<typename T>
        inline std::uint64_t get_64(ByteOrder order, ByteArray<T> data, std::size_t offset = 0)
        {
            runtime_assert((offset + 8) <= data.length, "Invalid offset");

            if (order == ByteOrder::LITTLE) {
                return (std::uint64_t(data[offset + 0]) & 0xff)           //
                       | ((std::uint64_t(data[offset + 1]) & 0xff) << 8)  //
                       | ((std::uint64_t(data[offset + 2]) & 0xff) << 16) //
                       | ((std::uint64_t(data[offset + 3]) & 0xff) << 24) //
                       | ((std::uint64_t(data[offset + 4]) & 0xff) << 32) //
                       | ((std::uint64_t(data[offset + 5]) & 0xff) << 40) //
                       | ((std::uint64_t(data[offset + 6]) & 0xff) << 48) //
                       | ((std::uint64_t(data[offset + 7]) & 0xff) << 56);
            }
            return (std::uint64_t(data[offset + 7]) & 0xff)           //
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
