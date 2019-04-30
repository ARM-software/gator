/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Buffer.h"
#include "BufferUtils.h"
#include "Logging.h"
#include "Protocol.h"
#include "Sender.h"
#include "SessionData.h"
#include "lib/Assert.h"

#define mask (mSize - 1)
#define FRAME_HEADER_SIZE 3
#define INVALID_LAST_EVENT_TIME (~0ull)

Buffer::Buffer(const int32_t core, const FrameType frameType, const int size, sem_t * const readerSem)
        : mBuf(new char[size]),
          mReaderSem(readerSem),
          mCommitTime(gSessionData.mLiveRate),
          mWriterSem(),
          mSize(size),
          mReadPos(0),
          mWritePos(0),
          mCommitPos(0),
          mAvailable(true),
          mIsDone(false),
          mCore(core),
          mFrameType(frameType),
          mLastEventTime(INVALID_LAST_EVENT_TIME),
          mLastEventCore(core),
          mLastEventTid(0)
{
    if ((mSize & mask) != 0) {
        logg.logError("Buffer size is not a power of 2");
        handleException();
    }

    runtime_assert(mSize > 8192, "Buffer::mSize is too small");

    sem_init(&mWriterSem, 0, 0);

    if (mFrameType != FrameType::UNKNOWN) {
        frame();
    }
}

Buffer::~Buffer()
{
    delete[] mBuf;
    sem_destroy(&mWriterSem);
}

void Buffer::write(ISender * const sender)
{
    // acquire the data written to the buffer
    const int commitPos = mCommitPos.load(std::memory_order_acquire);
    // only we, the consumer, write this so relaxed load is fine
    const int readPos = mReadPos.load(std::memory_order_relaxed);

    if (commitPos == readPos) {
        return; // nothing to do
    }

    // determine the size of two halves
    int length1 = commitPos - readPos;
    char *buffer1 = mBuf + readPos;
    int length2 = 0;
    char *buffer2 = mBuf;
    // possible wrap around
    if (length1 < 0) {
        length1 = mSize - readPos;
        length2 = commitPos;
    }

    logg.logMessage("Sending data length1: %i length2: %i", length1, length2);

    constexpr std::size_t numberOfParts = 2;
    const lib::Span<const char, int> parts[numberOfParts] = { { buffer1, length1 }, { buffer2, length2 } };
    sender->writeDataParts( { parts, numberOfParts }, ResponseType::RAW);

    // release the space only after we have finished reading the data
    mReadPos.store(commitPos, std::memory_order_release);

    // send a notification that space is available
    sem_post(&mWriterSem);
}


int Buffer::bytesAvailable() const
{
    int filled = mWritePos - mReadPos.load(std::memory_order_acquire);
    if (filled < 0) {
        filled += mSize;
    }

    int remaining = mSize - filled;

    if (mAvailable) {
        // Give some extra room; also allows space to insert the overflow error packet
        remaining -= 200;
    }
    else {
        // Hysteresis, prevents multiple overflow messages
        remaining -= 2000;
    }

    return remaining;
}

bool Buffer::checkSpace(const int bytes)
{
    const int remaining = bytesAvailable();

    if (remaining < bytes) {
        mAvailable = false;
    }
    else {
        mAvailable = true;
    }

    return mAvailable;
}

int Buffer::contiguousSpaceAvailable() const
{
    int remaining = bytesAvailable();
    int contiguous = mSize - mWritePos;
    if (remaining < contiguous) {
        return remaining;
    }
    else {
        return contiguous;
    }
}

bool Buffer::commit(const uint64_t time, const bool force)
{
    // post-populate the length, which does not include the response type length nor the length itself, i.e. only the length of the payload
    const int typeLength = gSessionData.mLocalCapture ? 0 : 1;
    // only we, the producer, write to mCommitPos so only relaxed load needed
    const int commitPos = mCommitPos.load(std::memory_order_relaxed);
    int length = mWritePos - commitPos;
    if (length < 0) {
        length += mSize;
    }
    length = length - typeLength - sizeof(int32_t);
    if (!force && !mIsDone && length <= FRAME_HEADER_SIZE) {
        // Nothing to write, only the frame header is present
        return false;
    }
    for (size_t byte = 0; byte < sizeof(int32_t); byte++) {
        mBuf[(commitPos + typeLength + byte) & mask] = (length >> byte * 8) & 0xFF;
    }

    logg.logMessage("Committing data mReadPos: %i mWritePos: %i mCommitPos: %i", mReadPos.load(std::memory_order_relaxed), mWritePos, commitPos);
    // release the commited data for the consumer to acquire
    mCommitPos.store(mWritePos, std::memory_order_release);

    if (gSessionData.mLiveRate > 0) {
        while (time > mCommitTime) {
            mCommitTime += gSessionData.mLiveRate;
        }
    }

    if ((!mIsDone) && (mFrameType != FrameType::UNKNOWN)) {
        frame();
    }

    // send a notification that data is ready
    sem_post(mReaderSem);
    return true;
}

bool Buffer::check(const uint64_t time)
{
    // only we, the producer, write to mCommitPos so only relaxed load needed
    int filled = mWritePos - mCommitPos.load(std::memory_order_relaxed);
    if (filled < 0) {
        filled += mSize;
    }
    if (filled >= ((mSize * 3) / 4) || (gSessionData.mLiveRate > 0 && time >= mCommitTime)) {
        return commit(time);
    }
    return false;
}

int Buffer::packInt(int32_t x)
{
    return buffer_utils::packInt(mBuf, mWritePos, x, mSize - 1);
}

int Buffer::packInt64(int64_t x)
{
    return buffer_utils::packInt64(mBuf, mWritePos, x, mSize - 1);
}

void Buffer::writeBytes(const void * const data, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        mBuf[(mWritePos + i) & mask] = static_cast<const char *>(data)[i];
    }

    mWritePos = (mWritePos + i) & mask;
}

void Buffer::writeString(const char * const str)
{
    const int len = strlen(str);
    packInt(len);
    writeBytes(str, len);
}

void Buffer::frame()
{
    runtime_assert(mFrameType != FrameType::UNKNOWN, "mFrameType == FrameType::UNKNOWN");

    beginFrameOrMessage(mFrameType, mCore, true);
}

int Buffer::beginFrameOrMessage(FrameType frameType, int32_t core)
{
    runtime_assert((mFrameType == frameType) || (mFrameType == FrameType::UNKNOWN), "invalid frameType");
    runtime_assert((mCore == core) || (mFrameType == FrameType::UNKNOWN), "invalid core");

    return beginFrameOrMessage(frameType, core, false);
}

bool Buffer::frameTypeSendsCpu(FrameType frameType)
{
   return ((frameType == FrameType::BLOCK_COUNTER) || (frameType == FrameType::PERF_ATTRS) || (frameType == FrameType::PERF_DATA)
                    || (frameType == FrameType::PERF_SYNC)
                    || (frameType == FrameType::NAME) || (frameType == FrameType::SCHED_TRACE));
}

int Buffer::beginFrameOrMessage(FrameType frameType, int32_t core, bool force)
{
    force |= (mFrameType == FrameType::UNKNOWN);

    if (force)
    {
        if (!gSessionData.mLocalCapture) {
            packInt(static_cast<int32_t>(ResponseType::APC_DATA));
        }

        const int result = mWritePos;

        // Reserve space for the length
        mWritePos += sizeof(int32_t);

        packInt(static_cast<int32_t>(frameType));
        if (frameTypeSendsCpu(frameType)) {
            packInt(core);
            mLastEventTime = INVALID_LAST_EVENT_TIME;
            mLastEventCore = core;
            mLastEventTid = 0;
        }

        return result;
    }
    else
    {
        return mWritePos;
    }
}

void Buffer::endFrame(uint64_t currTime, bool abort, int writePos)
{
    if (abort) {
        mWritePos = writePos;
    }
    else if (mFrameType == FrameType::UNKNOWN) {
        commit(currTime);
    }
    else {
        check(currTime);
    }
}

bool Buffer::eventHeader(const uint64_t curr_time)
{
    if (checkSpace(buffer_utils::MAXSIZE_PACK32 + buffer_utils::MAXSIZE_PACK64)) {
        // key of zero indicates a timestamp
        packInt(0);
        packInt64(curr_time);

        mLastEventTime = curr_time;
        mLastEventTid = 0; // this is also reset in CommonProtocolV22 when timestamp changes

        return true;
    }

    return false;
}

bool Buffer::eventCore(const int core)
{
    if (checkSpace(2 * buffer_utils::MAXSIZE_PACK32)) {
        // key of 2 indicates a core
        packInt(2);
        packInt(core);

        mLastEventCore = core;

        return true;
    }

    return false;
}

bool Buffer::eventTid(const int tid)
{
    if (checkSpace(2 * buffer_utils::MAXSIZE_PACK32)) {
        // key of 1 indicates a tid
        packInt(1);
        packInt(tid);

        mLastEventTid = tid;

        return true;
    }

    return false;
}

bool Buffer::event(const int key, const int32_t value)
{
    if (checkSpace(2 * buffer_utils::MAXSIZE_PACK32)) {
        packInt(key);
        packInt(value);

        return true;
    }

    return false;
}

bool Buffer::event64(const int key, const int64_t value)
{
    if (checkSpace(buffer_utils::MAXSIZE_PACK64 + buffer_utils::MAXSIZE_PACK32)) {
        packInt(key);
        packInt64(value);

        return true;
    }

    return false;
}

bool Buffer::counterMessage(uint64_t curr_time, int core, int key, int64_t value)
{
    return threadCounterMessage(curr_time, core, 0, key, value);
}

bool Buffer::threadCounterMessage(uint64_t curr_time, int core, int tid, int key, int64_t value)
{
    if ((mLastEventTime != curr_time) || (mLastEventTime == INVALID_LAST_EVENT_TIME)) {
        if (!eventHeader(curr_time)) {
            return false;
        }
    }

    if (mLastEventCore != core) {
        if (!eventCore(core)) {
            return false;
        }
    }

    if (mLastEventTid != tid) {
        if (!eventTid(tid)) {
            return false;
        }
    }

    if (!event64(key, value)) {
        return false;
    }

    check(curr_time);

    return true;
}

void Buffer::setDone()
{
    mIsDone = true;
    commit(0);
}

// This looks to be racey, see SDDAP-9064
bool Buffer::isDone() const
{
    if (!mIsDone) {
        return false;
    }
    const int commitPos = mCommitPos.load(std::memory_order_relaxed);
    if (commitPos != mWritePos) {
        return false;
    }
    return commitPos == mReadPos.load(std::memory_order_relaxed);
}
