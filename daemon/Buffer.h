/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

#ifndef BUFFER_H
#define BUFFER_H

#include "IBufferControl.h"
#include "IRawFrameBuilder.h"
#ifdef BUFFER_USE_SESSION_DATA
#include "SessionData.h"
#endif

#include <atomic>
#include <cstdint>

#include <semaphore.h>

class Buffer : public IBufferControl, public IRawFrameBuilderWithDirectAccess {
public:
    Buffer(int size, sem_t & readerSem, bool includeResponseType);
#ifdef BUFFER_USE_SESSION_DATA
    // include SessionData.h first to get access to this constructor
    Buffer(const int size, sem_t & readerSem) : Buffer(size, readerSem, !gSessionData.mLocalCapture) {}
#endif

    // Intentionally unimplemented
    Buffer(const Buffer &) = delete;
    Buffer & operator=(const Buffer &) = delete;
    Buffer(Buffer &&) = delete;
    Buffer & operator=(Buffer &&) = delete;

    ~Buffer() override;

    bool write(ISender & sender) override;

    int bytesAvailable() const override;
    bool isFull() const override { return bytesAvailable() <= 0; }
    int contiguousSpaceAvailable() const;
    int size() const { return mSize; }

    void setDone() override;

    // Prefer a new member to using these functions if possible
    char * getWritePos() { return mBuf + mWritePos; }

    int getWriteIndex() const override;
    void advanceWrite(int bytes) override;
    void writeDirect(int index, const void * data, std::size_t count) override;

    int packInt(int32_t x) override;
    int packInt64(int64_t x) override;
    void writeBytes(const void * data, std::size_t count) override;
    void writeString(const char * str) override;

    void beginFrame(FrameType frameType) override;
    void abortFrame() override;
    void endFrame() override;

    bool needsFlush() override;
    void flush() override;

    void waitForSpace(int bytes) override;
    bool supportsWriteOfSize(int bytes) const override;

private:
    char * const mBuf;
    sem_t & mReaderSem;
    sem_t mWriterSem;
    const int mSize;
    std::atomic_int mReadPos;
    int mWritePos;
    std::atomic_int mCommitPos;
    std::atomic_bool mIsDone;
    const bool mIncludeResponseType;
};

#endif // BUFFER_H
