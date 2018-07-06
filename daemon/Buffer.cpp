/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Buffer.h"
#include "Logging.h"
#include "Sender.h"
#include "SessionData.h"
#include "lib/Assert.h"
#include  <cstring>

#define mask (mSize - 1)
#define FRAME_HEADER_SIZE 3
#define INVALID_LAST_EVENT_TIME (~0ull)

Buffer::Buffer(const int32_t core, const int32_t buftype, const int size, sem_t * const readerSem)
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
          mBufType(buftype),
          mLastEventTime(INVALID_LAST_EVENT_TIME),
          mLastEventCore(core),
          mLastEventTid(0)
{
    if ((mSize & mask) != 0) {
        logg.logError("Buffer size is not a power of 2");
        handleException();
    }
    sem_init(&mWriterSem, 0, 0);

    if (mBufType != FRAME_UNKNOWN) {
        frame();
    }
}

Buffer::~Buffer()
{
    delete[] mBuf;
    sem_destroy(&mWriterSem);
}

void Buffer::write(Sender * const sender)
{
    if (!commitReady()) {
        return;
    }

    // commit and read are updated by the writer, only read them once
    int commitPos = mCommitPos;
    int readPos = mReadPos;

    // determine the size of two halves
    int length1 = commitPos - readPos;
    char *buffer1 = mBuf + readPos;
    int length2 = 0;
    char *buffer2 = mBuf;
    if (length1 < 0) {
        length1 = mSize - readPos;
        length2 = commitPos;
    }

    logg.logMessage("Sending data length1: %i length2: %i", length1, length2);

    // start, middle or end
    if (length1 > 0) {
        sender->writeData(buffer1, length1, RESPONSE_APC_DATA);
    }

    // possible wrap around
    if (length2 > 0) {
        sender->writeData(buffer2, length2, RESPONSE_APC_DATA);
    }

    mReadPos = commitPos;

    // send a notification that space is available
    sem_post(&mWriterSem);
}

bool Buffer::commitReady() const
{
    return mCommitPos != mReadPos;
}

int Buffer::bytesAvailable() const
{
    int filled = mWritePos - mReadPos;
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

bool Buffer::hasUncommittedMessages() const
{
    const int typeLength = gSessionData.mLocalCapture ? 0 : 1;
    int length = mWritePos - mCommitPos;
    if (length < 0) {
        length += mSize;
    }
    length = length - typeLength - sizeof(int32_t);
    return length > FRAME_HEADER_SIZE;
}

void Buffer::commit(const uint64_t time, const bool force)
{
    // post-populate the length, which does not include the response type length nor the length itself, i.e. only the length of the payload
    const int typeLength = gSessionData.mLocalCapture ? 0 : 1;
    int length = mWritePos - mCommitPos;
    if (length < 0) {
        length += mSize;
    }
    length = length - typeLength - sizeof(int32_t);
    if (!force && !mIsDone && length <= FRAME_HEADER_SIZE) {
        // Nothing to write, only the frame header is present
        return;
    }
    for (size_t byte = 0; byte < sizeof(int32_t); byte++) {
        mBuf[(mCommitPos + typeLength + byte) & mask] = (length >> byte * 8) & 0xFF;
    }

    logg.logMessage("Committing data mReadPos: %i mWritePos: %i mCommitPos: %i", mReadPos, mWritePos, mCommitPos);
    mCommitPos = mWritePos;

    if (gSessionData.mLiveRate > 0) {
        while (time > mCommitTime) {
            mCommitTime += gSessionData.mLiveRate;
        }
    }

    if ((!mIsDone) && (mBufType != FRAME_UNKNOWN)) {
        frame();
    }

    // send a notification that data is ready
    sem_post(mReaderSem);
}

void Buffer::check(const uint64_t time)
{
    int filled = mWritePos - mCommitPos;
    if (filled < 0) {
        filled += mSize;
    }
    if (filled >= ((mSize * 3) / 4) || (gSessionData.mLiveRate > 0 && time >= mCommitTime)) {
        commit(time);
    }
}

int Buffer::sizeOfPackInt(int32_t x)
{
    char tmp[Buffer::MAXSIZE_PACK32];
    int writePos = 0;

    return Buffer::packInt(tmp, Buffer::MAXSIZE_PACK32, writePos, x);
}

int Buffer::sizeOfPackInt64(int64_t x)
{
    char tmp[Buffer::MAXSIZE_PACK64];
    int writePos = 0;

    return Buffer::packInt64(tmp, Buffer::MAXSIZE_PACK64, writePos, x);
}

int Buffer::packInt(char * const buf, const int size, int &writePos, int32_t x)
{
    int packedBytes = 0;
    int more = true;
    while (more) {
        // low order 7 bits of x
        char b = x & 0x7f;
        x >>= 7;

        if ((x == 0 && (b & 0x40) == 0) || (x == -1 && (b & 0x40) != 0)) {
            more = false;
        }
        else {
            b |= 0x80;
        }

        buf[(writePos + packedBytes) & /*mask*/(size - 1)] = b;
        packedBytes++;
    }

    writePos = (writePos + packedBytes) & /*mask*/(size - 1);

    return packedBytes;
}

int Buffer::packInt(int32_t x)
{
    return packInt(mBuf, mSize, mWritePos, x);
}

int Buffer::packInt64(char * const buf, const int size, int &writePos, int64_t x)
{
    int packedBytes = 0;
    int more = true;
    while (more) {
        // low order 7 bits of x
        char b = x & 0x7f;
        x >>= 7;

        if ((x == 0 && (b & 0x40) == 0) || (x == -1 && (b & 0x40) != 0)) {
            more = false;
        }
        else {
            b |= 0x80;
        }

        buf[(writePos + packedBytes) & /*mask*/(size - 1)] = b;
        packedBytes++;
    }

    writePos = (writePos + packedBytes) & /*mask*/(size - 1);

    return packedBytes;
}

int Buffer::packInt64(int64_t x)
{
    return packInt64(mBuf, mSize, mWritePos, x);
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
    runtime_assert(mBufType != FRAME_UNKNOWN, "mBufType == FRAME_UNKNOWN");

    beginFrameOrMessage(mBufType, mCore, true);
}

int Buffer::beginFrameOrMessage(int32_t frameType, int32_t core)
{
    runtime_assert((mBufType == frameType) || (mBufType == FRAME_UNKNOWN), "invalid frameType");
    runtime_assert((mCore == core) || (mBufType == FRAME_UNKNOWN), "invalid core");

    return beginFrameOrMessage(frameType, core, false);
}

bool Buffer::frameTypeSendsCpu(int32_t frameType)
{
   return ((frameType == FRAME_BLOCK_COUNTER) || (frameType == FRAME_PERF_ATTRS) || (frameType == FRAME_PERF)
                    || (frameType == FRAME_NAME) || (frameType == FRAME_SCHED_TRACE));
}

int Buffer::beginFrameOrMessage(int32_t frameType, int32_t core, bool force)
{
    force |= (mBufType == FRAME_UNKNOWN);

    if (force)
    {
        if (!gSessionData.mLocalCapture) {
            packInt(RESPONSE_APC_DATA);
        }

        const int result = mWritePos;

        // Reserve space for the length
        mWritePos += sizeof(int32_t);

        packInt(frameType);
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
    else if (mBufType == FRAME_UNKNOWN) {
        commit(currTime);
    }
    else {
        check(currTime);
    }
}

void Buffer::summary(const uint64_t currTime, const int64_t timestamp, const int64_t uptime,
                     const int64_t monotonicDelta, const char * const uname, const long pageSize, const bool nosync,
                     const std::map<const char *, const char *> & additionalAttributes)
{
    packInt(MESSAGE_SUMMARY);
    writeString(NEWLINE_CANARY);
    packInt64(timestamp);
    packInt64(uptime);
    packInt64(monotonicDelta);
    writeString("uname");
    writeString(uname);
    writeString("PAGESIZE");
    char buf[32];
    snprintf(buf, sizeof(buf), "%li", pageSize);
    writeString(buf);
    if (nosync) {
        writeString("nosync");
        writeString("");
    }
    for (const auto & pair : additionalAttributes) {
        if (strlen(pair.first) > 0) {
            writeString(pair.first);
            writeString(pair.second);
        }
    }
    writeString("");
    check(currTime);
}

void Buffer::coreName(const uint64_t currTime, const int core, const int cpuid, const char * const name)
{
    if (checkSpace(3 * MAXSIZE_PACK32 + 0x100)) {
        packInt(MESSAGE_CORE_NAME);
        packInt(core);
        packInt(cpuid);
        writeString(name);
    }
    check(currTime);
}

bool Buffer::eventHeader(const uint64_t curr_time)
{
    if (checkSpace(MAXSIZE_PACK32 + MAXSIZE_PACK64)) {
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
    if (checkSpace(2 * MAXSIZE_PACK32)) {
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
    if (checkSpace(2 * MAXSIZE_PACK32)) {
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
    if (checkSpace(2 * MAXSIZE_PACK32)) {
        packInt(key);
        packInt(value);

        return true;
    }

    return false;
}

bool Buffer::event64(const int key, const int64_t value)
{
    if (checkSpace(MAXSIZE_PACK64 + MAXSIZE_PACK32)) {
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

void Buffer::marshalPea(const uint64_t currTime, const struct perf_event_attr * const pea, int key)
{
    while (!checkSpace(2 * MAXSIZE_PACK32 + pea->size)) {
        sem_wait(&mWriterSem);
    }
    packInt(CODE_PEA);
    writeBytes(pea, pea->size);
    packInt(key);
    check(currTime);
}

void Buffer::marshalKeys(const uint64_t currTime, const int count, const uint64_t * const ids, const int * const keys)
{
    while (!checkSpace(2 * MAXSIZE_PACK32 + count * (MAXSIZE_PACK32 + MAXSIZE_PACK64))) {
        sem_wait(&mWriterSem);
    }
    packInt(CODE_KEYS);
    packInt(count);
    for (int i = 0; i < count; ++i) {
        packInt64(ids[i]);
        packInt(keys[i]);
    }
    check(currTime);
}

void Buffer::marshalKeysOld(const uint64_t currTime, const int keyCount, const int * const keys, const int bytes,
                            const char * const buf)
{
    while (!checkSpace((2 + keyCount) * MAXSIZE_PACK32 + bytes)) {
        sem_wait(&mWriterSem);
    }
    packInt(CODE_KEYS_OLD);
    packInt(keyCount);
    for (int i = 0; i < keyCount; ++i) {
        packInt(keys[i]);
    }
    writeBytes(buf, bytes);
    check(currTime);
}

void Buffer::marshalFormat(const uint64_t currTime, const int length, const char * const format)
{
    while (!checkSpace(MAXSIZE_PACK32 + length + 1)) {
        sem_wait(&mWriterSem);
    }
    packInt(CODE_FORMAT);
    writeBytes(format, length + 1);
    check(currTime);
}

void Buffer::marshalMaps(const uint64_t currTime, const int pid, const int tid, const char * const maps)
{
    const int mapsLen = strlen(maps) + 1;
    while (!checkSpace(3 * MAXSIZE_PACK32 + mapsLen)) {
        sem_wait(&mWriterSem);
    }
    packInt(CODE_MAPS);
    packInt(pid);
    packInt(tid);
    writeBytes(maps, mapsLen);
    check(currTime);
}

void Buffer::marshalComm(const uint64_t currTime, const int pid, const int tid, const char * const image,
                         const char * const comm)
{
    const int imageLen = strlen(image) + 1;
    const int commLen = strlen(comm) + 1;
    while (!checkSpace(3 * MAXSIZE_PACK32 + imageLen + commLen)) {
        sem_wait(&mWriterSem);
    }
    packInt(CODE_COMM);
    packInt(pid);
    packInt(tid);
    writeBytes(image, imageLen);
    writeBytes(comm, commLen);
    check(currTime);
}

void Buffer::onlineCPU(const uint64_t currTime, const int cpu)
{
    while (!checkSpace(MAXSIZE_PACK32 + MAXSIZE_PACK64)) {
        sem_wait(&mWriterSem);
    }
    packInt(CODE_ONLINE_CPU);
    packInt64(currTime);
    packInt(cpu);
    check(currTime);
}

void Buffer::offlineCPU(const uint64_t currTime, const int cpu)
{
    while (!checkSpace(MAXSIZE_PACK32 + MAXSIZE_PACK64)) {
        sem_wait(&mWriterSem);
    }
    packInt(CODE_OFFLINE_CPU);
    packInt64(currTime);
    packInt(cpu);
    check(currTime);
}

void Buffer::marshalKallsyms(const uint64_t currTime, const char * const kallsyms)
{
    const int kallsymsLen = strlen(kallsyms) + 1;
    while (!checkSpace(3 * MAXSIZE_PACK32 + kallsymsLen)) {
        sem_wait(&mWriterSem);
    }
    packInt(CODE_KALLSYMS);
    writeBytes(kallsyms, kallsymsLen);
    check(currTime);
}

void Buffer::perfCounterHeader(const uint64_t time)
{
    while (!checkSpace(MAXSIZE_PACK32 + MAXSIZE_PACK64)) {
        sem_wait(&mWriterSem);
    }
    packInt(CODE_COUNTERS);
    packInt64(time);
}

void Buffer::perfCounter(const int core, const int key, const int64_t value)
{
    while (!checkSpace(2 * MAXSIZE_PACK32 + MAXSIZE_PACK64)) {
        sem_wait(&mWriterSem);
    }
    packInt(core);
    packInt(key);
    packInt64(value);
}

void Buffer::perfCounterFooter(const uint64_t currTime)
{
    while (!checkSpace(MAXSIZE_PACK32)) {
        sem_wait(&mWriterSem);
    }
    packInt(-1);
    check(currTime);
}

void Buffer::marshalHeaderPage(const uint64_t currTime, const char * const headerPage)
{
    const int headerPageLen = strlen(headerPage) + 1;
    while (!checkSpace(MAXSIZE_PACK32 + headerPageLen)) {
        sem_wait(&mWriterSem);
    }
    packInt(CODE_HEADER_PAGE);
    writeBytes(headerPage, headerPageLen);
    check(currTime);
}

void Buffer::marshalHeaderEvent(const uint64_t currTime, const char * const headerEvent)
{
    const int headerEventLen = strlen(headerEvent) + 1;
    while (!checkSpace(MAXSIZE_PACK32 + headerEventLen)) {
        sem_wait(&mWriterSem);
    }
    packInt(CODE_HEADER_EVENT);
    writeBytes(headerEvent, headerEventLen);
    check(currTime);
}

void Buffer::setDone()
{
    mIsDone = true;
    commit(0);
}

bool Buffer::isDone() const
{
    return mIsDone && mReadPos == mCommitPos && mCommitPos == mWritePos;
}
