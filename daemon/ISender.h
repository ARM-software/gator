/* Copyright (C) 2010-2024 by Arm Limited. All rights reserved. */

#ifndef __ISENDER_H__
#define __ISENDER_H__

#include "lib/Span.h"

#include <cstdint>
#include <string>

enum class ResponseType : char {
    /// Special value used by ISender meaning do not frame the response.
    RAW = 0,

    // Actual values understood by streamline
    XML = 1,
    APC_DATA = 3,
    ACK = 4,
    NAK = 5,
    CURRENT_CONFIG = 6,
    GATOR_LOG = 7,
    ACTIVITY_STARTED = 8,
    ERROR = '\xFF'
};

class ISender {
public:
    /**
     * @param dataParts must be a complete response unless type is RAW
     */
    virtual void writeDataParts(lib::Span<const lib::Span<const uint8_t, int>> dataParts,
                                ResponseType type,
                                bool ignoreLockErrors = false) = 0;

    void writeData(const uint8_t * data, int length, ResponseType type, bool ignoreLockErrors = false)
    {
        lib::Span<const uint8_t, int> dataSpan = {data, length};
        writeDataParts(lib::Span<const lib::Span<const uint8_t, int>> {&dataSpan, 1}, type, ignoreLockErrors);
    }

    void writeData(const std::string & string_data, ResponseType type, bool ignoreLockErrors = false)
    {
        writeData(reinterpret_cast<const std::uint8_t *>(string_data.data()),
                  string_data.length(),
                  type,
                  ignoreLockErrors);
    }

    virtual ~ISender() = default;

    static constexpr int MAX_RESPONSE_LENGTH = 256 * 1024 * 1024;
};

#endif //__ISENDER_H__
