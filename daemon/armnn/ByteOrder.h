/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

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

        /**
         * Convert the endianness to/ from the system endianness.
         * E.g. will change the endianess of the data if the order is not equal to the systems byte order.
        */
        inline std::uint16_t convertEndianness(ByteOrder order, std::uint16_t data)
        {
            if (order == SYSTEM)
            {
                return data;
            }
            else
            {
                return ((((data) >> 8) & 0x00FF) | (((data) << 8) & 0xFF00));
            }
        }

        inline std::uint32_t convertEndianness(ByteOrder order, std::uint32_t data)
        {
            if (order == SYSTEM)
            {
                return data;
            }
            else
            {
                return ((((data) >> 24) & 0x000000FF) | (((data) >>  8) & 0x0000FF00) \
                        | (((data) <<  8) & 0x00FF0000) | (((data) << 24) & 0xFF000000));
            }
        }

        inline std::uint64_t convertEndianness(ByteOrder order, std::uint64_t data)
        {
            if (order == SYSTEM)
            {
                return data;
            }
            else
            {
                return ((((data) >> 56) & 0x00000000000000FF) | (((data) >> 40) & 0x000000000000FF00) \
                        | (((data) >> 24) & 0x0000000000FF0000) | (((data) >>  8) & 0x00000000FF000000) \
                        | (((data) <<  8) & 0x000000FF00000000) | (((data) << 24) & 0x0000FF0000000000) \
                        | (((data) << 40) & 0x00FF000000000000) | (((data) << 56) & 0xFF00000000000000));
            }
        }

        /** Read an aligned 16-bit value from some byte array. The caller is responsible for ensuring the access is aligned and within bounds */
        template<typename T>
        inline std::uint16_t get_aligned_16(ByteOrder order, ByteArray<T> data, std::size_t offset)
        {
            runtime_assert((offset + 2) <= data.length, "Invalid offset");

            const std::uint16_t result = *reinterpret_cast<const std::uint16_t *>(&(data[offset]));

            if (order == SYSTEM) {
                return result;
            }
            else {
                return ((result >> 8) & 0x00ff) //
                       | ((result << 8) & 0xff00);
            }
        }

        /** Read an aligned 32-bit value from some byte array. The caller is responsible for ensuring the access is aligned and within bounds */
        template<typename T>
        inline std::uint32_t get_aligned_32(ByteOrder order, ByteArray<T> data, std::size_t offset)
        {
            runtime_assert((offset + 4) <= data.length, "Invalid offset");

            const std::uint32_t result = *reinterpret_cast<const std::uint32_t *>(&(data[offset]));

            if (order == SYSTEM) {
                return result;
            }
            else {
                return ((result >> 24) & 0x000000ff)  //
                       | ((result >> 8) & 0x0000ff00) //
                       | ((result << 8) & 0x00ff0000) //
                       | ((result << 24) & 0xff000000);
            }
        }

        /** Read an aligned 64-bit value from some byte array. The caller is responsible for ensuring the access is aligned and within bounds */
        template<typename T>
        inline std::uint64_t get_aligned_64(ByteOrder order, ByteArray<T> data, std::size_t offset)
        {
            runtime_assert((offset + 8) <= data.length, "Invalid offset");

            const std::uint64_t result = *reinterpret_cast<const std::uint64_t *>(&(data[offset]));

            if (order == SYSTEM) {
                return result;
            }
            else {
                return ((result >> 56) & 0x00000000000000ffull)   //
                       | ((result >> 40) & 0x000000000000ff00ull) //
                       | ((result >> 24) & 0x0000000000ff0000ull) //
                       | ((result >> 8) & 0x00000000ff000000ull)  //
                       | ((result << 8) & 0x000000ff00000000ull)  //
                       | ((result << 24) & 0x0000ff0000000000ull) //
                       | ((result << 40) & 0x00ff000000000000ull) //
                       | ((result << 56) & 0xff00000000000000ull);
            }
        }

        /** Read an unaligned 16-bit value from some byte array. */
        template<typename T>
        inline std::uint16_t get_unaligned_16(ByteOrder order, ByteArray<T> data, std::size_t offset)
        {
            runtime_assert((offset + 2) <= data.length, "Invalid offset");

            if (order == ByteOrder::LITTLE) {
                return (std::uint16_t(data[offset + 0]) & 0xff) //
                       | ((std::uint16_t(data[offset + 1]) & 0xff) << 8);
            }
            else {
                return (std::uint16_t(data[offset + 1]) & 0xff) //
                       | ((std::uint16_t(data[offset + 0]) & 0xff) << 8);
            }
        }

        /** Read an unaligned 32-bit value from some byte array. */
        template<typename T>
        inline std::uint32_t get_unaligned_32(ByteOrder order, ByteArray<T> data, std::size_t offset)
        {
            runtime_assert((offset + 4) <= data.length, "Invalid offset");

            if (order == ByteOrder::LITTLE) {
                return (std::uint32_t(data[offset + 0]) & 0xff)           //
                       | ((std::uint32_t(data[offset + 1]) & 0xff) << 8)  //
                       | ((std::uint32_t(data[offset + 2]) & 0xff) << 16) //
                       | ((std::uint32_t(data[offset + 3]) & 0xff) << 24);
            }
            else {
                return (std::uint32_t(data[offset + 3]) & 0xff)           //
                       | ((std::uint32_t(data[offset + 2]) & 0xff) << 8)  //
                       | ((std::uint32_t(data[offset + 1]) & 0xff) << 16) //
                       | ((std::uint32_t(data[offset + 0]) & 0xff) << 24);
            }
        }

        /** Read an unaligned 64-bit value from some byte array. */
        template<typename T>
        inline std::uint64_t get_unaligned_64(ByteOrder order, ByteArray<T> data, std::size_t offset)
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
            else {
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
}

#endif // INCLUDE_ARMNN_BYTE_ORDER_H
