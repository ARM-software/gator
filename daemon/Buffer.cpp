/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#include "Buffer.h"

#include "BufferUtils.h"
#include "Logging.h"
#include "Protocol.h"
#include "Sender.h"
#include "lib/Assert.h"

#include <cstring>

#define mask (mSize - 1)
#define FRAME_HEADER_SIZE 1 // single byte of FrameType

// Fraction of size that should be kept free
// if less than that is free we should send
constexpr int FRACTION_TO_KEEP_FREE = 4;

Buffer::Buffer(const int size, sem_t & readerSem, bool includeResponseType)
    : mBuf(new char[size]),
      mReaderSem(readerSem),
      mWriterSem(),
      mSize(size),
      mReadPos(0),
      mWritePos(0),
      mCommitPos(0),
      mIsDone(false),
      mIncludeResponseType(includeResponseType)
{
    if ((mSize & mask) != 0) {
        delete[] mBuf;
        logg.logError("Buffer size is not a power of 2");
        handleException();
    }

    runtime_assert(mSize > 8192, "Buffer::mSize is too small");

    sem_init(&mWriterSem, 0, 0);
}

Buffer::~Buffer()
{
    delete[] mBuf;
    sem_destroy(&mWriterSem);
}

bool Buffer::write(ISender & sender)
{
    bool isDone = mIsDone.load(std::memory_order_acquire);
    // acquire the data written to the buffer
    const int commitPos = mCommitPos.load(std::memory_order_acquire);
    // only we, the consumer, write this so relaxed load is fine
    const int readPos = mReadPos.load(std::memory_order_relaxed);

    if (commitPos == readPos) {
        // nothing to do
        return isDone;
    }

    // determine the size of two halves
    int length1 = commitPos - readPos;
    char * buffer1 = mBuf + readPos;
    int length2 = 0;
    char * buffer2 = mBuf;
    // possible wrap around
    if (length1 < 0) {
        length1 = mSize - readPos;
        length2 = commitPos;
    }

    logg.logMessage("Sending data length1: %i length2: %i", length1, length2);

    constexpr std::size_t numberOfParts = 2;
    const lib::Span<const char, int> parts[numberOfParts] = {{buffer1, length1}, {buffer2, length2}};
    sender.writeDataParts({parts, numberOfParts}, ResponseType::RAW);

    // release the space only after we have finished reading the data
    mReadPos.store(commitPos, std::memory_order_release);

    // send a notification that space is available
    sem_post(&mWriterSem);

    return isDone;
}

int Buffer::bytesAvailable() const
{
    int filled = mWritePos - mReadPos.load(std::memory_order_acquire);
    if (filled < 0) {
        filled += mSize;
    }

    int remaining = mSize - filled;

    // Give some extra room;
    // this is required because in one shot mode we check this to see
    // if we've filled the buffer. We might get to the end and need to write
    // say 8 bytes but there's only 1 byte left, for the purpose of one-shot,
    // this is full.
    remaining -= 200;

    return std::max(remaining, 0);
}

void Buffer::waitForSpace(int bytes)
{
    if (bytes > mSize) {
        logg.logError("Buffer not big enough, %d but need %d", mSize, bytes);
        handleException();
    }

    while (bytesAvailable() < bytes) {
        sem_wait(&mWriterSem);
    }
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

void Buffer::flush()
{
    if (mCommitPos.load(std::memory_order_relaxed) != mReadPos.load(std::memory_order_acquire)) {
        // send a notification that data is ready
        sem_post(&mReaderSem);
    }
}

bool Buffer::needsFlush()
{
    // only we, the producer, write to mCommitPos so only relaxed load needed
    int filled = mWritePos - mCommitPos.load(std::memory_order_relaxed);
    if (filled < 0) {
        filled += mSize;
    }
    return filled >= ((mSize * (FRACTION_TO_KEEP_FREE - 1)) / FRACTION_TO_KEEP_FREE);
}

int Buffer::packInt(int32_t x)
{
    return buffer_utils::packInt(mBuf, mWritePos, x, mSize - 1);
}

int Buffer::packInt64(int64_t x)
{
    return buffer_utils::packInt64(mBuf, mWritePos, x, mSize - 1);
}

void Buffer::writeBytes(const void * const data, std::size_t count)
{
    std::size_t i = 0;

    for (; i < count; ++i) {
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

void Buffer::beginFrame(FrameType frameType)
{
    if (mIncludeResponseType) {
        packInt(static_cast<int32_t>(ResponseType::APC_DATA));
    }

    // Reserve space for the length
    mWritePos = (mWritePos + sizeof(int32_t)) & mask;

    packInt(static_cast<int32_t>(frameType));
}

void Buffer::abortFrame()
{
    mWritePos = mCommitPos;
}

void Buffer::endFrame()
{
    // post-populate the length, which does not include the response type length nor the length itself, i.e. only the length of the payload
    const int typeLength = mIncludeResponseType ? 1 : 0;
    // only we, the producer, write to mCommitPos so only relaxed load needed
    const int commitPos = mCommitPos.load(std::memory_order_relaxed);
    int length = mWritePos - commitPos;
    if (length < 0) {
        length += mSize;
    }
    length = length - typeLength - sizeof(int32_t);
    if (length <= FRAME_HEADER_SIZE) {
        // Nothing to write, only the frame header is present
        abortFrame();
        return;
    }
    for (size_t byte = 0; byte < sizeof(int32_t); byte++) {
        mBuf[(commitPos + typeLength + byte) & mask] = (length >> byte * 8) & 0xFF;
    }

    logg.logMessage("Committing data mReadPos: %i mWritePos: %i mCommitPos: %i",
                    mReadPos.load(std::memory_order_relaxed),
                    mWritePos,
                    commitPos);
    // release the commited data for the consumer to acquire
    mCommitPos.store(mWritePos, std::memory_order_release);
}

void Buffer::setDone()
{
    mIsDone.store(true, std::memory_order_release);
    // notify sender we're done (EOF).
    // need to do this even if no new data
    // as sender waits for new data *and* EOF
    sem_post(&mReaderSem);
}

int Buffer::getWriteIndex() const
{
    return mWritePos;
}

void Buffer::advanceWrite(int bytes)
{
    mWritePos = (mWritePos + bytes) & /*mask*/ (mSize - 1);
}

void Buffer::writeDirect(int index, const void * data, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        mBuf[(index + i) & mask] = static_cast<const char *>(data)[i];
    }
}
