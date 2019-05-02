/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfSyncThreadBuffer.h"
#include "BufferUtils.h"
#include "ISender.h"

std::vector<std::unique_ptr<PerfSyncThreadBuffer>> PerfSyncThreadBuffer::create(std::uint64_t monotonicRawBase, bool supportsClockId, bool hasSPEConfiguration, sem_t & senderSem)
{
    // the number of cores to enable:
    // * If the user wanted to capture SPE data, then send thread for all of them as we need per-core VCNT data
    // * If the user did not select SPE, but the kernel does not support clock_id then just sync on core 0
    // * Otherwise no sync required
    const unsigned count = (hasSPEConfiguration ? std::thread::hardware_concurrency()
                                                : (supportsClockId ? 0
                                                                   : 1));

    std::vector<std::unique_ptr<PerfSyncThreadBuffer>> result;

    // fill result
    for (unsigned cpu = 0; cpu < count; ++cpu) {
        const bool enableSyncThreadMode = (!supportsClockId) && (cpu == 0);
        const bool readTimer = hasSPEConfiguration;
        result.emplace_back(new PerfSyncThreadBuffer(monotonicRawBase, cpu, enableSyncThreadMode, readTimer, &senderSem));
    }

    return result;
}

PerfSyncThreadBuffer::PerfSyncThreadBuffer(std::uint64_t monotonicRawBase, unsigned cpu, bool enableSyncThreadMode, bool readTimer,
                                           sem_t * readerSem)
        : monotonicRawBase(monotonicRawBase),
          buffer(cpu, FrameType::PERF_SYNC, 1024 * 1024, readerSem),
          thread(cpu, enableSyncThreadMode, readTimer, monotonicRawBase, [this] (unsigned c, pid_t p, pid_t t, std::uint64_t f, std::uint64_t cmr, std::uint64_t vcnt) {
              write(c, p, t, cmr, vcnt, f);
          })
{
}

void PerfSyncThreadBuffer::terminate()
{
    thread.terminate();
    buffer.setDone();
}

bool PerfSyncThreadBuffer::complete() const
{
    return buffer.isDone();
}

void PerfSyncThreadBuffer::write(unsigned, pid_t pid, pid_t tid, std::uint64_t monotonicRaw, std::uint64_t vcnt,
                                 std::uint64_t freq)
{
    if (buffer.isDone()) {
        return;
    }

    // make sure there is space for at least one more record
    const int minBytesRequired = ((1 * buffer_utils::MAXSIZE_PACK64) + (3 * buffer_utils::MAXSIZE_PACK32))
                               + (2 * buffer_utils::MAXSIZE_PACK64);


    // wait for write space
    buffer.waitForSpace(minBytesRequired);

    // write header
    buffer.packInt(pid);
    buffer.packInt(tid);
    buffer.packInt64(freq);

    // write record
    buffer.packInt64(monotonicRaw);
    buffer.packInt64(vcnt);

    // commit data (always do this so that the record is pushed to the host in live mode in a timely fashion)
    buffer.commit(monotonicRaw - monotonicRawBase, true);
}

void PerfSyncThreadBuffer::send(ISender & sender)
{
    buffer.write(&sender);
}
