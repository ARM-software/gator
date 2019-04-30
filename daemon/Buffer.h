/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef BUFFER_H
#define BUFFER_H

#include "ClassBoilerPlate.h"

#include <cstdint>
#include <semaphore.h>
#include <atomic>

#include "Protocol.h"
#include "IBuffer.h"

class ISender;

class Buffer : public IBuffer
{
public:
    Buffer(int32_t core, FrameType frameType, const int size, sem_t * const readerSem);
    ~Buffer();

    void write(ISender *sender);

    int bytesAvailable() const;
    int contiguousSpaceAvailable() const;
    bool commit(const uint64_t time, const bool force = false);
    bool check(const uint64_t time);

    // Block Counter messages
    bool eventHeader(uint64_t curr_time);
    bool eventCore(int core);
    bool eventTid(int tid);
    bool event(int key, int32_t value);
    bool event64(int key, int64_t value);

    bool counterMessage(uint64_t curr_time, int core, int key, int64_t value);
    bool threadCounterMessage(uint64_t curr_time, int core, int tid, int key, int64_t value);

    void setDone();
    bool isDone() const;

    // Prefer a new member to using these functions if possible
    char *getWritePos()
    {
        return mBuf + mWritePos;
    }
    void advanceWrite(int bytes)
    {
        mWritePos = (mWritePos + bytes) & /*mask*/(mSize - 1);
    }

    FrameType getFrameType() const
    {
        return mFrameType;
    }

    int packInt(int32_t x);
    int packInt64(int64_t x);
    void writeBytes(const void * const data, size_t count);
    void writeString(const char * const str);

    int beginFrameOrMessage(FrameType frameType, int32_t core);
    void endFrame(uint64_t currTime, bool abort, int writePos);

    void waitForSpace(int bytes) {
        while (!checkSpace(bytes)) {
            sem_wait(&mWriterSem);
        }
    }

private:

    bool frameTypeSendsCpu(FrameType frameType);
    int beginFrameOrMessage(FrameType frameType, int32_t core, bool force);
    void frame();
    bool checkSpace(int bytes);

    char * const mBuf;
    sem_t * const mReaderSem;
    uint64_t mCommitTime;
    sem_t mWriterSem;
    const int mSize;
    std::atomic_int mReadPos;
    int mWritePos;
    std::atomic_int mCommitPos;
    bool mAvailable;
    bool mIsDone;
    const int32_t mCore;
    const FrameType mFrameType;
    uint64_t mLastEventTime;
    int mLastEventCore;
    int mLastEventTid;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(Buffer);
};

#endif // BUFFER_H
