/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include "Protocol.h"

#include <cstddef>
#include <cstdint>

/**
 * Builds an arbitrary APC frame
 */
class IRawFrameBuilder {

public:
    static constexpr const int MAX_FRAME_HEADER_SIZE =
        /* reponse type */ 1 + /* length */ sizeof(int32_t) + /* frame type */ 1;

    virtual ~IRawFrameBuilder() = default;

    /**
     * Begins a new frame
     *
     * There must be no current frame
     */
    virtual void beginFrame(FrameType frameType) = 0;

    /**
     * Aborts the current frame
     *
     * There must be a current frame
     * There will be no current frame afterwards
     */
    virtual void abortFrame() = 0;

    /**
     * Ends the current frame and commits it to the buffer
     *
     * There must be a current frame
     * There will be no current frame afterwards
     * Does not flush the buffer
     */
    virtual void endFrame() = 0;

    /**
     * Checks if the backing buffer needs flushing
     * @return true if flushing is needed
     */
    virtual bool needsFlush() = 0;

    /**
     * Flushes all frames commited to the buffer
     */
    virtual void flush() = 0;

    /**
     * Gets the number of bytes available in the backing buffer
     */
    virtual int bytesAvailable() const = 0;

    /**
     * Packs a 32 bit number
     *
     * Must be required bytes available
     */
    virtual int packInt(int32_t x) = 0;

    /**
     * Packs a 64 bit number
     *
     * Must be required bytes available
     */
    virtual int packInt64(int64_t x) = 0;

    /**
     * Writes some arbitrary bytes to the frame
     *
     * Must be required bytes available
     */
    virtual void writeBytes(const void * data, std::size_t count) = 0;

    /**
     * Writes a string to the frame
     *
     * Must be required bytes available
     */
    virtual void writeString(const char * str) = 0;

    // TODO: add some method like this so a read call can write directly to the buffer
    // virtual void getContiguousSpace(function<int /* number used */ (lib::Span<char> /* backing buffer */)> consumer) = 0;

    /**
     * Waits for some space to become available.
     */
    virtual void waitForSpace(int bytes) = 0;
};
