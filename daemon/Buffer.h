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
#include <map>
#include <semaphore.h>

#include "k/perf_event.h"

class Sender;

// all values *must* be in range 0 ... 127 so as to fit in exactly one byte of packedInt
enum
{
    FRAME_UNKNOWN = 0,
    FRAME_SUMMARY = 1,
    FRAME_NAME = 3,
    FRAME_COUNTER = 4,
    FRAME_BLOCK_COUNTER = 5,
    FRAME_SCHED_TRACE = 7,
    FRAME_EXTERNAL = 10,
    FRAME_PERF_ATTRS = 11,
    FRAME_PROC = 11,
    FRAME_PERF = 12,
    FRAME_ACTIVITY_TRACE = 13,
};

// PERF_ATTR messages
enum
{
    CODE_PEA = 1,
    CODE_KEYS = 2,
    CODE_FORMAT = 3,
    CODE_MAPS = 4,
    CODE_COMM = 5,
    CODE_KEYS_OLD = 6,
    CODE_ONLINE_CPU = 7,
    CODE_OFFLINE_CPU = 8,
    CODE_KALLSYMS = 9,
    CODE_COUNTERS = 10,
    CODE_HEADER_PAGE = 11,
    CODE_HEADER_EVENT = 12,
};

// Summary Frame Messages
enum
{
    MESSAGE_LINK = 1,
    MESSAGE_SUMMARY = 1,
    MESSAGE_SCHED_SWITCH = 1,
    MESSAGE_COOKIE_NAME = 1,
    MESSAGE_THREAD_NAME = 2,
    MESSAGE_THREAD_EXIT = 2,
    MESSAGE_CORE_NAME = 3,
    MESSAGE_TASK_EXIT = 3,
};

// From gator_marshaling.c
#define NEWLINE_CANARY \
    /* Unix */ \
    "1\n" \
    /* Windows */ \
    "2\r\n" \
    /* Mac OS */ \
    "3\r" \
    /* RISC OS */ \
    "4\n\r" \
    /* Add another character so the length isn't 0x0a bytes */ \
    "5"

class Buffer
{
public:
    static constexpr const size_t MAXSIZE_PACK32 = 5;
    static constexpr const size_t MAXSIZE_PACK64 = 10;
    static constexpr const size_t MAX_FRAME_HEADER_SIZE = (2 * MAXSIZE_PACK32) + sizeof(int32_t);

    Buffer(int32_t core, int32_t buftype, const int size, sem_t * const readerSem);
    ~Buffer();

    void write(Sender *sender);

    int bytesAvailable() const;
    int contiguousSpaceAvailable() const;
    bool hasUncommittedMessages() const;
    void commit(const uint64_t time, const bool force = false);
    void check(const uint64_t time);

    // Summary messages
    void summary(const uint64_t currTime, const int64_t timestamp, const int64_t uptime, const int64_t monotonicDelta,
                 const char * const uname, const long pageSize, const bool nosync,
                 const std::map<const char *, const char *> & additionalAttributes);
    void coreName(const uint64_t currTime, const int core, const int cpuid, const char * const name);

    // Block Counter messages
    bool eventHeader(uint64_t curr_time);
    bool eventCore(int core);
    bool eventTid(int tid);
    bool event(int key, int32_t value);
    bool event64(int key, int64_t value);

    bool counterMessage(uint64_t curr_time, int core, int key, int64_t value);
    bool threadCounterMessage(uint64_t curr_time, int core, int tid, int key, int64_t value);

    // Perf Attrs messages
    void marshalPea(const uint64_t currTime, const struct perf_event_attr * const pea, int key);
    void marshalKeys(const uint64_t currTime, const int count, const uint64_t * const ids, const int * const keys);
    void marshalKeysOld(const uint64_t currTime, const int keyCount, const int * const keys, const int bytes,
                        const char * const buf);
    void marshalFormat(const uint64_t currTime, const int length, const char * const format);
    void marshalMaps(const uint64_t currTime, const int pid, const int tid, const char * const maps);
    void marshalComm(const uint64_t currTime, const int pid, const int tid, const char * const image,
                     const char * const comm);
    void onlineCPU(const uint64_t currTime, const int cpu);
    void offlineCPU(const uint64_t currTime, const int cpu);
    void marshalKallsyms(const uint64_t currTime, const char * const kallsyms);
    void perfCounterHeader(const uint64_t time);
    void perfCounter(const int core, const int key, const int64_t value);
    void perfCounterFooter(const uint64_t currTime);
    void marshalHeaderPage(const uint64_t currTime, const char * const headerPage);
    void marshalHeaderEvent(const uint64_t currTime, const char * const headerEvent);

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

    int getFrameType() const
    {
        return mBufType;
    }

    static int sizeOfPackInt(int32_t x);
    static int sizeOfPackInt64(int64_t x);
    static int packInt(char * const buf, const int size, int &writePos, int32_t x);
    static int packInt64(char * const buf, const int size, int &writePos, int64_t x);

    int packInt(int32_t x);
    int packInt64(int64_t x);
    void writeBytes(const void * const data, size_t count);
    void writeString(const char * const str);

    int beginFrameOrMessage(int32_t frameType, int32_t core);
    void endFrame(uint64_t currTime, bool abort, int writePos);

    static void writeLEInt(unsigned char *buf, uint32_t v)
    {
        buf[0] = (v >> 0) & 0xFF;
        buf[1] = (v >> 8) & 0xFF;
        buf[2] = (v >> 16) & 0xFF;
        buf[3] = (v >> 24) & 0xFF;
    }

    static void writeLELong(unsigned char *buf, uint64_t v)
    {
        buf[0] = (v >> 0) & 0xFF;
        buf[1] = (v >> 8) & 0xFF;
        buf[2] = (v >> 16) & 0xFF;
        buf[3] = (v >> 24) & 0xFF;
        buf[4] = (v >> 32) & 0xFF;
        buf[5] = (v >> 40) & 0xFF;
        buf[6] = (v >> 48) & 0xFF;
        buf[7] = (v >> 56) & 0xFF;
    }

private:

    bool frameTypeSendsCpu(int32_t frameType);
    int beginFrameOrMessage(int32_t frameType, int32_t core, bool force);
    void frame();
    bool commitReady() const;
    bool checkSpace(int bytes);

    char * const mBuf;
    sem_t * const mReaderSem;
    uint64_t mCommitTime;
    sem_t mWriterSem;
    const int mSize;
    int mReadPos;
    int mWritePos;
    int mCommitPos;
    bool mAvailable;
    bool mIsDone;
    const int32_t mCore;
    const int32_t mBufType;
    uint64_t mLastEventTime;
    int mLastEventCore;
    int mLastEventTid;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(Buffer);
};

#endif // BUFFER_H
