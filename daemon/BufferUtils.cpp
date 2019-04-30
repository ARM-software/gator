/**
 * Copyright (C) Arm Limited 2013-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "BufferUtils.h"

namespace buffer_utils
{
    int sizeOfPackInt(int32_t x)
    {
        char tmp[MAXSIZE_PACK32];
        int writePos = 0;

        return packInt(tmp, writePos, x);
    }

    int sizeOfPackInt64(int64_t x)
    {
        char tmp[MAXSIZE_PACK64];
        int writePos = 0;

        return packInt64(tmp, writePos, x);
    }

    int packInt(char * const buf, int &writePos, int32_t x, int writePosWrapMask)
    {
        int packedBytes = 0;
        int more = true;
        while (more) {
            // low order 7 bits of x
            char b = x & 0x7f;
            x >>= 7;

            if ((x == 0 && (b & 0x40) == 0) || (x == -1 && (b & 0x40) != 0)) {
                more = false;
            }
            else {
                b |= 0x80;
            }

            buf[(writePos + packedBytes) & writePosWrapMask] = b;
            packedBytes++;
        }

        writePos = (writePos + packedBytes) & writePosWrapMask;

        return packedBytes;
    }

    int packInt64(char * const buf, int &writePos, int64_t x, int writePosWrapMask)
    {
        int packedBytes = 0;
        int more = true;
        while (more) {
            // low order 7 bits of x
            char b = x & 0x7f;
            x >>= 7;

            if ((x == 0 && (b & 0x40) == 0) || (x == -1 && (b & 0x40) != 0)) {
                more = false;
            }
            else {
                b |= 0x80;
            }

            buf[(writePos + packedBytes) & writePosWrapMask] = b;
            packedBytes++;
        }

        writePos = (writePos + packedBytes) & writePosWrapMask;

        return packedBytes;
    }

    int32_t unpackInt(const char * buf, int &readPos)
    {
        uint8_t shift = 0;
        int32_t value = 0;
        char b = -1;

        while ((b & 0x80) != 0) {
            b = buf[readPos++];
            value |= int32_t(b & 0x7f) << shift;
            shift += 7;
        }

        if (shift < 8 * sizeof(value) && (b & 0x40) != 0) {
            value |= -(INT32_C(1) << shift);
        }

        return value;
    }

    int64_t unpackInt64(const char * buf, int &readPos)
    {
        uint8_t shift = 0;
        int64_t value = 0;
        char b = -1;

        while ((b & 0x80) != 0) {
            b = buf[readPos++];
            value |= int64_t(b & 0x7f) << shift;
            shift += 7;
        }

        if (shift < 8 * sizeof(value) && (b & 0x40) != 0) {
            value |= -(INT64_C(1) << shift);
        }

        return value;
    }

}

