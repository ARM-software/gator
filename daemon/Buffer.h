/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef BUFFER_H
#define BUFFER_H

#include "IBuffer.h"
#include "Protocol.h"
#ifdef BUFFER_USE_SESSION_DATA
#include "SessionData.h"
#endif

#include <atomic>
#include <cstdint>
#include <semaphore.h>

class ISender;

class Buffer : public IBuffer {
public:
    Buffer(int32_t core,
           FrameType frameType,
           int size,
           sem_t & readerSem,
           uint64_t commitRate,
           bool includeResponseType);
#ifdef BUFFER_USE_SESSION_DATA
    // include SessionData.h first to get access to this constructor
    Buffer(int32_t core, FrameType frameType, const int size, sem_t & readerSem)
        : Buffer(core, frameType, size, readerSem, gSessionData.mLiveRate, !gSessionData.mLocalCapture)
    {
    }
#endif
    ~Buffer() override;

    void write(ISender & sender) override;

    int bytesAvailable() const override;
    int contiguousSpaceAvailable() const;
    bool commit(uint64_t time, bool force = false);
    bool check(uint64_t time) override;

    // Block Counter messages
    bool eventHeader(uint64_t curr_time) override;
    bool eventCore(int core) override;
    bool eventTid(int tid) override;
    bool event64(int key, int64_t value) override;

    bool threadCounterMessage(uint64_t curr_time, int core, int tid, int key, int64_t value) override;

    void setDone() override;
    bool isDone() const override;

    // Prefer a new member to using these functions if possible
    char * getWritePos() { return mBuf + mWritePos; }
    void advanceWrite(int bytes) { mWritePos = (mWritePos + bytes) & /*mask*/ (mSize - 1); }

    FrameType getFrameType() const { return mFrameType; }

    int packInt(int32_t x) override;
    int packInt64(int64_t x) override;
    void writeBytes(const void * data, size_t count) override;
    void writeString(const char * str);

    int beginFrameOrMessage(FrameType frameType, int32_t core);
    void endFrame(uint64_t currTime, bool abort, int writePos);

    // Will commit if needed.
    void waitForSpace(int bytes, uint64_t currTime);

private:
    static bool frameTypeSendsCpu(FrameType frameType);
    int beginFrameOrMessage(FrameType frameType, int32_t core, bool force);
    void frame();
    bool checkSpace(int bytes) const;

    char * const mBuf;
    sem_t & mReaderSem;
    const uint64_t mCommitRate;
    uint64_t mCommitTime;
    sem_t mWriterSem;
    const int mSize;
    std::atomic_int mReadPos;
    int mWritePos;
    std::atomic_int mCommitPos;
    bool mIsDone;
    const bool mIncludeResponseType;
    const int32_t mCore;
    const FrameType mFrameType;
    uint64_t mLastEventTime;
    int mLastEventCore;
    int mLastEventTid;

    // Intentionally unimplemented
    Buffer(const Buffer &) = delete;
    Buffer & operator=(const Buffer &) = delete;
    Buffer(Buffer &&) = delete;
    Buffer & operator=(Buffer &&) = delete;
};

#endif // BUFFER_H
