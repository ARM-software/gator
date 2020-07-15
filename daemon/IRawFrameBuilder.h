/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include <cstdint>

/**
 * Builds an arbitrary APC frame
 */
class IRawFrameBuilder {

public:
    virtual ~IRawFrameBuilder() = default;

    /**
     * Gets the number of bytes available in the backing buffer
     */
    virtual int bytesAvailable() const = 0;

    /**
     * Packs a 32 bit number
     */
    virtual int packInt(int32_t x) = 0;

    /**
     * Packs a 64 bit number
     */
    virtual int packInt64(int64_t x) = 0;

    /**
     * Writes some arbitrary bytes to the frame
     */
    virtual void writeBytes(const void * data, std::size_t count) = 0;

    // TODO: add some method like this so a read call can write directly to the buffer
    // virtual void getContiguousSpace(function<int /* number used */ (lib::Span<char> /* backing buffer */)> consumer) = 0;

    /**
     * commits the currently built up frame if needed
     * @return true if the current frame was committed
     */
    // TODO: rename to commitIfNeeded
    virtual bool check(uint64_t time) = 0;
};
