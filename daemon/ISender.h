/**
 * Copyright (C) Arm Limited 2010-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ISENDER_H__
#define __ISENDER_H__

#include "lib/Span.h"

enum class ResponseType : char
{
    /// Special value used by ISender meaning do not frame the response.
    RAW = 0,
    /// Special value used by gator.ko meaning do not put type in the frame.
    NONE = 0,

    // Actual values understood by streamline
    XML = 1,
    APC_DATA = 3,
    ACK = 4,
    NAK = 5,
    ERROR = '\xFF'
};

class ISender
{
public:
    /**
     *
     * @param dataParts must be a complete response unless type is RAW
     * @param type
     * @param ignoreLockErrors
     */
    virtual void writeDataParts(lib::Span<const lib::Span<const char, int>> dataParts, ResponseType type, bool ignoreLockErrors = false) = 0;

    void writeData(const char* data, int length, ResponseType type, bool ignoreLockErrors = false)
    {
        lib::Span<const char, int> dataSpan = { data, length };
        writeDataParts(lib::Span<const lib::Span<const char, int>> { &dataSpan, 1 }, type, ignoreLockErrors);
    }

    virtual ~ISender() = default;

    static constexpr int MAX_RESPONSE_LENGTH = 16 * 1024 * 1024;
};

#endif //__ISENDER_H__
