/* Copyright (C) 2018-2021 by Arm Limited. All rights reserved. */

#define BUFFER_USE_SESSION_DATA

#include "linux/perf/PerfSyncThreadBuffer.h"

#include "BufferUtils.h"
#include "ISender.h"
#include "SessionData.h"

std::unique_ptr<PerfSyncThreadBuffer> PerfSyncThreadBuffer::create(bool supportsClockId,
                                                                   bool hasSPEConfiguration,
                                                                   sem_t & senderSem)
{
    std::unique_ptr<PerfSyncThreadBuffer> result;

    // fill result
    if (hasSPEConfiguration || !supportsClockId) {
        const bool enableSyncThreadMode = (!supportsClockId);
        const bool readTimer = hasSPEConfiguration;
        result = std::make_unique<PerfSyncThreadBuffer>(enableSyncThreadMode, readTimer, senderSem);
    }

    return result;
}

PerfSyncThreadBuffer::PerfSyncThreadBuffer(bool enableSyncThreadMode, bool readTimer, sem_t & readerSem)
    : buffer(1024 * 1024, readerSem),
      thread(enableSyncThreadMode,
             readTimer,
             [this](pid_t p, pid_t t, std::uint64_t f, std::uint64_t cmr, std::uint64_t vcnt) {
                 write(p, t, cmr, vcnt, f);
             })
{
}

void PerfSyncThreadBuffer::start(std::uint64_t monotonicRawBase)
{
    thread.start(monotonicRawBase);
}

void PerfSyncThreadBuffer::terminate()
{
    thread.terminate();
}

void PerfSyncThreadBuffer::write(pid_t pid,
                                 pid_t tid,
                                 std::uint64_t monotonicRaw,
                                 std::uint64_t vcnt,
                                 std::uint64_t freq)
{
    // make sure there is space for at least one more record
    const int minBytesRequired = IRawFrameBuilder::MAX_FRAME_HEADER_SIZE + buffer_utils::MAXSIZE_PACK32
                               + ((1 * buffer_utils::MAXSIZE_PACK64) + (3 * buffer_utils::MAXSIZE_PACK32))
                               + (2 * buffer_utils::MAXSIZE_PACK64);

    // wait for write space
    buffer.waitForSpace(minBytesRequired);

    buffer.beginFrame(FrameType::PERF_SYNC);
    buffer.packInt(0); // just pass CPU == 0, Since Streamline 7.4 it is ignored anyway

    // write header
    buffer.packInt(pid);
    buffer.packInt(tid);
    buffer.packInt64(freq);

    // write record
    buffer.packInt64(monotonicRaw);
    buffer.packInt64(vcnt);

    buffer.endFrame();
    // commit data (always do this so that the record is pushed to the host in live mode in a timely fashion)
    buffer.flush();
}

void PerfSyncThreadBuffer::send(ISender & sender)
{
    buffer.write(sender);
}
