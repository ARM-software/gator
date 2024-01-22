/* Copyright (C) 2020-2023 by Arm Limited. All rights reserved. */

#include "BlockCounterFrameBuilder.h"

#include "BufferUtils.h"
#include "IRawFrameBuilder.h"
#include "Protocol.h"

#include <cstdint>

BlockCounterFrameBuilder::~BlockCounterFrameBuilder()
{
    endFrame();
}

bool BlockCounterFrameBuilder::eventHeader(uint64_t time)
{
    if (!ensureFrameStarted()) {
        return false;
    }

    if (checkSpace(buffer_utils::MAXSIZE_PACK32 + buffer_utils::MAXSIZE_PACK64)) {
        // key of zero indicates a timestamp
        rawBuilder.packInt(0);
        rawBuilder.packInt64(time);

        return true;
    }

    return false;
}

bool BlockCounterFrameBuilder::eventCore(int core)
{
    if (!ensureFrameStarted()) {
        return false;
    }

    if (checkSpace(2 * buffer_utils::MAXSIZE_PACK32)) {
        // key of 2 indicates a core
        rawBuilder.packInt(2);
        rawBuilder.packInt(core);

        return true;
    }

    return false;
}

bool BlockCounterFrameBuilder::eventTid(int tid)
{
    if (!ensureFrameStarted()) {
        return false;
    }

    if (checkSpace(2 * buffer_utils::MAXSIZE_PACK32)) {
        // key of 1 indicates a tid
        rawBuilder.packInt(1);
        rawBuilder.packInt(tid);

        return true;
    }

    return false;
}

bool BlockCounterFrameBuilder::event64(int key, int64_t value)
{
    if (!ensureFrameStarted()) {
        return false;
    }

    if (checkSpace(buffer_utils::MAXSIZE_PACK64 + buffer_utils::MAXSIZE_PACK32)) {
        rawBuilder.packInt(key);
        rawBuilder.packInt64(value);

        return true;
    }

    return false;
}

bool BlockCounterFrameBuilder::check(const uint64_t time)
{
    if ((flushIsNeeded != nullptr) && ((*flushIsNeeded)(time, rawBuilder.needsFlush()))) {
        return flush();
    }
    return false;
}

bool BlockCounterFrameBuilder::flush()
{
    const bool shouldEndFrame = endFrame();
    rawBuilder.flush();
    return shouldEndFrame;
}

bool BlockCounterFrameBuilder::checkSpace(const int bytes)
{
    return rawBuilder.bytesAvailable() >= bytes;
}

bool BlockCounterFrameBuilder::ensureFrameStarted()
{
    if (isFrameStarted) {
        return true;
    }

    if (!checkSpace(IRawFrameBuilder::MAX_FRAME_HEADER_SIZE + buffer_utils::MAXSIZE_PACK32)) {
        return false;
    }

    rawBuilder.beginFrame(FrameType::BLOCK_COUNTER);
    rawBuilder.packInt(0); // core
    isFrameStarted = true;
    return true;
}

bool BlockCounterFrameBuilder::endFrame()
{
    const bool shouldEndFrame = isFrameStarted;
    if (shouldEndFrame) {
        rawBuilder.endFrame();
        isFrameStarted = false;
    }
    return shouldEndFrame;
}
