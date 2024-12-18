/* Copyright (C) 2013-2024 by Arm Limited. All rights reserved. */

#ifndef BUFFER_UTILS_H
#define BUFFER_UTILS_H

#include <cstddef>
#include <cstdint>

namespace buffer_utils {
    static constexpr const int MAXSIZE_PACK32 = 5;
    static constexpr const int MAXSIZE_PACK64 = 10;

    int packInt(uint8_t * buf, int & writePos, int32_t x, int writePosWrapMask = -1);
    int packInt64(uint8_t * buf, int & writePos, int64_t x, int writePosWrapMask = -1);

    int32_t unpackInt(const uint8_t * buf, int & readPos);
    int64_t unpackInt64(const uint8_t * buf, int & readPos);

    int sizeOfPackInt(int32_t x);
    int sizeOfPackInt64(int64_t x);

    inline void writeLEInt(uint8_t * buf, uint32_t v)
    {
        buf[0] = (v >> 0) & 0xFF;
        buf[1] = (v >> 8) & 0xFF;
        buf[2] = (v >> 16) & 0xFF;
        buf[3] = (v >> 24) & 0xFF;
    }

    inline void writeLEInt(uint8_t * buf, uint32_t v, int & writePos)
    {
        writeLEInt(buf + writePos, v);
        writePos += sizeof(uint32_t);
    }

    inline uint32_t readLEInt(const uint8_t * buf)
    {
        return (static_cast<uint32_t>(static_cast<uint8_t>(buf[0])) << 0)
             | (static_cast<uint32_t>(static_cast<uint8_t>(buf[1])) << 8)
             | (static_cast<uint32_t>(static_cast<uint8_t>(buf[2])) << 16)
             | (static_cast<uint32_t>(static_cast<uint8_t>(buf[3])) << 24);
    }

    inline uint32_t readLEInt(const uint8_t * buf, int & readPos)
    {
        const uint32_t v = readLEInt(buf + readPos);
        readPos += sizeof(uint32_t);
        return v;
    }

    inline void writeLELong(uint8_t * buf, uint64_t v)
    {
        buf[0] = (v >> 0) & 0xFF;
        buf[1] = (v >> 8) & 0xFF;
        buf[2] = (v >> 16) & 0xFF;
        buf[3] = (v >> 24) & 0xFF;
        buf[4] = (v >> 32) & 0xFF;
        buf[5] = (v >> 40) & 0xFF;
        buf[6] = (v >> 48) & 0xFF;
        buf[7] = (v >> 56) & 0xFF;
    }

    inline void writeLELong(uint8_t * buf, uint64_t v, int & writePos)
    {
        writeLELong(buf + writePos, v);
        writePos += sizeof(uint64_t);
    }

    inline uint64_t readLELong(const char * buf)
    {
        return (static_cast<uint64_t>(static_cast<uint8_t>(buf[0])) << 0)
             | (static_cast<uint64_t>(static_cast<uint8_t>(buf[1])) << 8)
             | (static_cast<uint64_t>(static_cast<uint8_t>(buf[2])) << 16)
             | (static_cast<uint64_t>(static_cast<uint8_t>(buf[3])) << 24)
             | (static_cast<uint64_t>(static_cast<uint8_t>(buf[4])) << 32)
             | (static_cast<uint64_t>(static_cast<uint8_t>(buf[5])) << 40)
             | (static_cast<uint64_t>(static_cast<uint8_t>(buf[6])) << 48)
             | (static_cast<uint64_t>(static_cast<uint8_t>(buf[7])) << 56);
    }

    inline uint64_t readLELong(const uint8_t * buf, int & readPos)
    {
        const uint64_t v = readLEInt(buf + readPos);
        readPos += sizeof(uint64_t);
        return v;
    }

}

#endif // BUFFER_UTILS_H
