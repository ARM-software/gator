/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#define BUFFER_USE_SESSION_DATA

#include "PerfAttrsBuffer.h"

#include "BufferUtils.h"
#include "SessionData.h"
#include "k/perf_event.h"

#include <cstring>

PerfAttrsBuffer::PerfAttrsBuffer(const int size, sem_t * const readerSem)
    : buffer(0 /* ignored */, FrameType::PERF_ATTRS, size, readerSem)
{
}

void PerfAttrsBuffer::write(ISender * const sender)
{
    buffer.write(sender);
}

int PerfAttrsBuffer::bytesAvailable() const
{
    return buffer.bytesAvailable();
}

void PerfAttrsBuffer::commit(const uint64_t time)
{
    buffer.commit(time);
}

void PerfAttrsBuffer::setDone()
{
    buffer.setDone();
}

bool PerfAttrsBuffer::isDone() const
{
    return buffer.isDone();
}

void PerfAttrsBuffer::marshalPea(const uint64_t currTime, const struct perf_event_attr * const pea, int key)
{
    buffer.waitForSpace(2 * buffer_utils::MAXSIZE_PACK32 + pea->size, currTime);
    buffer.packInt(static_cast<int32_t>(CodeType::PEA));
    buffer.writeBytes(pea, pea->size);
    buffer.packInt(key);
    buffer.check(currTime);
}

void PerfAttrsBuffer::marshalKeys(const uint64_t currTime,
                                  const int count,
                                  const uint64_t * const ids,
                                  const int * const keys)
{
    buffer.waitForSpace(2 * buffer_utils::MAXSIZE_PACK32 +
                            count * (buffer_utils::MAXSIZE_PACK32 + buffer_utils::MAXSIZE_PACK64),
                        currTime);
    buffer.packInt(static_cast<int32_t>(CodeType::KEYS));
    buffer.packInt(count);
    for (int i = 0; i < count; ++i) {
        buffer.packInt64(ids[i]);
        buffer.packInt(keys[i]);
    }
    buffer.check(currTime);
}

void PerfAttrsBuffer::marshalKeysOld(const uint64_t currTime,
                                     const int keyCount,
                                     const int * const keys,
                                     const int bytes,
                                     const char * const buf)
{
    buffer.waitForSpace((2 + keyCount) * buffer_utils::MAXSIZE_PACK32 + bytes, currTime);
    buffer.packInt(static_cast<int32_t>(CodeType::KEYS_OLD));
    buffer.packInt(keyCount);
    for (int i = 0; i < keyCount; ++i) {
        buffer.packInt(keys[i]);
    }
    buffer.writeBytes(buf, bytes);
    buffer.check(currTime);
}

void PerfAttrsBuffer::marshalFormat(const uint64_t currTime, const int length, const char * const format)
{
    buffer.waitForSpace(buffer_utils::MAXSIZE_PACK32 + length + 1, currTime);
    buffer.packInt(static_cast<int32_t>(CodeType::FORMAT));
    buffer.writeBytes(format, length + 1);
    buffer.check(currTime);
}

void PerfAttrsBuffer::marshalMaps(const uint64_t currTime, const int pid, const int tid, const char * const maps)
{
    const int mapsLen = strlen(maps) + 1;
    buffer.waitForSpace(3 * buffer_utils::MAXSIZE_PACK32 + mapsLen, currTime);
    buffer.packInt(static_cast<int32_t>(CodeType::MAPS));
    buffer.packInt(pid);
    buffer.packInt(tid);
    buffer.writeBytes(maps, mapsLen);
    buffer.check(currTime);
}

void PerfAttrsBuffer::marshalComm(const uint64_t currTime,
                                  const int pid,
                                  const int tid,
                                  const char * const image,
                                  const char * const comm)
{
    const int imageLen = strlen(image) + 1;
    const int commLen = strlen(comm) + 1;
    buffer.waitForSpace(3 * buffer_utils::MAXSIZE_PACK32 + imageLen + commLen, currTime);
    buffer.packInt(static_cast<int32_t>(CodeType::COMM));
    buffer.packInt(pid);
    buffer.packInt(tid);
    buffer.writeBytes(image, imageLen);
    buffer.writeBytes(comm, commLen);
    buffer.check(currTime);
}

void PerfAttrsBuffer::onlineCPU(const uint64_t currTime, const int cpu)
{
    buffer.waitForSpace(buffer_utils::MAXSIZE_PACK32 + buffer_utils::MAXSIZE_PACK64, currTime);
    buffer.packInt(static_cast<int32_t>(CodeType::ONLINE_CPU));
    buffer.packInt64(currTime);
    buffer.packInt(cpu);
    buffer.check(currTime);
}

void PerfAttrsBuffer::offlineCPU(const uint64_t currTime, const int cpu)
{
    buffer.waitForSpace(buffer_utils::MAXSIZE_PACK32 + buffer_utils::MAXSIZE_PACK64, currTime);
    buffer.packInt(static_cast<int32_t>(CodeType::OFFLINE_CPU));
    buffer.packInt64(currTime);
    buffer.packInt(cpu);
    buffer.check(currTime);
}

void PerfAttrsBuffer::marshalKallsyms(const uint64_t currTime, const char * const kallsyms)
{
    const int kallsymsLen = strlen(kallsyms) + 1;
    buffer.waitForSpace(3 * buffer_utils::MAXSIZE_PACK32 + kallsymsLen, currTime);
    buffer.packInt(static_cast<int32_t>(CodeType::KALLSYMS));
    buffer.writeBytes(kallsyms, kallsymsLen);
    buffer.check(currTime);
}

void PerfAttrsBuffer::perfCounterHeader(const uint64_t currTime, const int numberOfCounters)
{
    // @formatter:off
    buffer.waitForSpace(
        // header (this function)
        buffer_utils::MAXSIZE_PACK32       // code type
            + buffer_utils::MAXSIZE_PACK64 // currTime
            // counters (perfCounter)
            + numberOfCounters * (buffer_utils::MAXSIZE_PACK32    // core
                                  + buffer_utils::MAXSIZE_PACK32  // key
                                  + buffer_utils::MAXSIZE_PACK64) // value
            // footer (perfCounterFooter)
            + buffer_utils::MAXSIZE_PACK32, // sentinel value
        currTime);
    // @formatter:on
    buffer.packInt(static_cast<int32_t>(CodeType::COUNTERS));
    buffer.packInt64(currTime);
}

void PerfAttrsBuffer::perfCounter(const int core, const int key, const int64_t value)
{
    buffer.packInt(core);
    buffer.packInt(key);
    buffer.packInt64(value);
}

void PerfAttrsBuffer::perfCounterFooter(const uint64_t currTime)
{
    buffer.packInt(-1);
    buffer.check(currTime);
}

void PerfAttrsBuffer::marshalHeaderPage(const uint64_t currTime, const char * const headerPage)
{
    const int headerPageLen = strlen(headerPage) + 1;
    buffer.waitForSpace(buffer_utils::MAXSIZE_PACK32 + headerPageLen, currTime);
    buffer.packInt(static_cast<int32_t>(CodeType::HEADER_PAGE));
    buffer.writeBytes(headerPage, headerPageLen);
    buffer.check(currTime);
}

void PerfAttrsBuffer::marshalHeaderEvent(const uint64_t currTime, const char * const headerEvent)
{
    const int headerEventLen = strlen(headerEvent) + 1;
    buffer.waitForSpace(buffer_utils::MAXSIZE_PACK32 + headerEventLen, currTime);
    buffer.packInt(static_cast<int32_t>(CodeType::HEADER_EVENT));
    buffer.writeBytes(headerEvent, headerEventLen);
    buffer.check(currTime);
}
