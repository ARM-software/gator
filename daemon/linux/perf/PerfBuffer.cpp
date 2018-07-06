/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "linux/perf/PerfBuffer.h"

#include <cstring>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "Buffer.h"
#include "Logging.h"
#include "Sender.h"
#include "SessionData.h"

std::size_t PerfBuffer::calculateBufferLength()
{
   return  (gSessionData.mPerfMmapSizeInPages > 1 ? (gSessionData.mPerfMmapSizeInPages * gSessionData.mPageSize)
                                                  : ((gSessionData.mTotalBufferSize * 1024 * 1024) & ~(gSessionData.mPageSize - 1)));
}

std::size_t PerfBuffer::calculateMMapLength()
{
    return (gSessionData.mPageSize + calculateBufferLength());
}

PerfBuffer::PerfBuffer()
        : mBuffers(),
          mDiscard()
{
}

PerfBuffer::~PerfBuffer()
{
    for (auto cpuAndBuf : mBuffers) {
        munmap(cpuAndBuf.second.buffer, gSessionData.mPageSize + calculateMMapLength() - 1);
    }
}

bool PerfBuffer::useFd(const int fd, int cpu)
{
    auto buffer = mBuffers.find(cpu);
    if (buffer != mBuffers.end()) {
        if (ioctl(fd, PERF_EVENT_IOC_SET_OUTPUT, buffer->second.fd) < 0) {
            logg.logMessage("ioctl failed for fd %i (errno=%d, %s)", fd, errno, strerror(errno));
            return false;
        }
    }
    else {
        const size_t mmapLength = calculateMMapLength();

        void * const buf = mmap(NULL, mmapLength, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

        if (buf == MAP_FAILED) {
            logg.logMessage("mmap failed for fd %i (errno=%d, %s, mmapLength=%zu)", fd, errno, strerror(errno), mmapLength);
            if ((errno == EPERM) && (geteuid() != 0)) {
                logg.logError("Could not mmap perf data buffer on cpu %d, EPERM returned.\n"
                        "This may be caused by too small limit in /proc/sys/kernel/perf_event_mlock_kb\n"
                        "Try again with a smaller value of --mmap-pages\n"
                        "Usually a value of ((perf_event_mlock_kb * 1024 / page_size) - 1) or lower will work.", cpu);
                if (gSessionData.mPerfMmapSizeInPages > 1) {
                    logg.logError("The current value for --mmap-pages is %d", gSessionData.mPerfMmapSizeInPages);
                }
            }
            return false;
        }

        mBuffers[cpu] = Buffer { fd, buf };

        // Check the version
        struct perf_event_mmap_page *pemp = static_cast<struct perf_event_mmap_page *>(buf);
        if (pemp->compat_version != 0) {
            logg.logMessage("Incompatible perf_event_mmap_page compat_version for fd %i", fd);
            return false;
        }
    }

    return true;
}

void PerfBuffer::discard(int cpu)
{
    if (mBuffers.find(cpu) != mBuffers.end()) {
        mDiscard.insert(cpu);
    }
}

bool PerfBuffer::isEmpty()
{
    for (auto cpuAndBuf : mBuffers) {
        // Take a snapshot of the positions
        struct perf_event_mmap_page *pemp = static_cast<struct perf_event_mmap_page *>(cpuAndBuf.second.buffer);
        const uint64_t head = ACCESS_ONCE(pemp->data_head);
        const uint64_t tail = ACCESS_ONCE(pemp->data_tail);

        if (head != tail) {
            return false;
        }
    }

    return true;
}

bool PerfBuffer::isFull()
{
    for (auto cpuAndBuf : mBuffers) {
        // Take a snapshot of the positions
        struct perf_event_mmap_page *pemp = static_cast<struct perf_event_mmap_page *>(cpuAndBuf.second.buffer);
        const uint64_t head = ACCESS_ONCE(pemp->data_head);

        if ((head + 2000) <= calculateBufferLength()) {
            return true;
        }
    }

    return false;
}

class PerfFrame
{
public:
    PerfFrame(Sender * const sender)
            : mSender(sender),
              mWritePos(-1),
              mCpuSizePos(-1)
    {
    }

    void add(const int cpu, const uint64_t head, uint64_t tail, const uint8_t * const b)
    {
        cpuHeader(cpu);

        const std::size_t bufferLength = PerfBuffer::calculateBufferLength();
        const std::size_t bufferMask = (bufferLength - 1);

        while (head > tail) {
            const int count = reinterpret_cast<const struct perf_event_header *>(b + (tail & bufferMask))->size
                    / sizeof(uint64_t);
            // Can this whole message be written as Streamline assumes events are not split between frames
            if (sizeof(mBuf) <= mWritePos + count * Buffer::MAXSIZE_PACK64) {
                send();
                cpuHeader(cpu);
            }
            for (int i = 0; i < count; ++i) {
                // Must account for message size
                Buffer::packInt64(mBuf, sizeof(mBuf), mWritePos,
                                  *reinterpret_cast<const uint64_t *>(b + (tail & bufferMask)));
                tail += sizeof(uint64_t);
            }
        }
    }

    void send()
    {
        if (mWritePos > 0) {
            writeFrameSize();
            mSender->writeData(mBuf, mWritePos, RESPONSE_APC_DATA);
            mWritePos = -1;
            mCpuSizePos = -1;
        }
    }

private:

    void writeFrameSize()
    {
        writeCpuSize();
        const int typeLength = gSessionData.mLocalCapture ? 0 : 1;
        Buffer::writeLEInt(reinterpret_cast<unsigned char *>(mBuf + typeLength),
                           mWritePos - typeLength - sizeof(uint32_t));
    }

    void frameHeader()
    {
        if (mWritePos < 0) {
            mWritePos = 0;
            mCpuSizePos = -1;
            if (!gSessionData.mLocalCapture) {
                mBuf[mWritePos++] = RESPONSE_APC_DATA;
            }
            // Reserve space for frame size
            mWritePos += sizeof(uint32_t);
            Buffer::packInt(mBuf, sizeof(mBuf), mWritePos, FRAME_PERF);
        }
    }

    void writeCpuSize()
    {
        if (mCpuSizePos >= 0) {
            Buffer::writeLEInt(reinterpret_cast<unsigned char *>(mBuf + mCpuSizePos),
                               mWritePos - mCpuSizePos - sizeof(uint32_t));
        }
    }

    void cpuHeader(const int cpu)
    {
        if (sizeof(mBuf) <= mWritePos + Buffer::MAXSIZE_PACK32 + sizeof(uint32_t)) {
            send();
        }
        frameHeader();
        writeCpuSize();
        Buffer::packInt(mBuf, sizeof(mBuf), mWritePos, cpu);
        mCpuSizePos = mWritePos;
        // Reserve space for cpu size
        mWritePos += sizeof(uint32_t);
    }

    // Pick a big size but something smaller than the chunkSize in Sender::writeData which is 100k
    char mBuf[1 << 16];
    Sender * const mSender;
    int mWritePos;
    int mCpuSizePos;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(PerfFrame);
};

bool PerfBuffer::send(Sender * const sender)
{
    PerfFrame frame(sender);

    for (auto cpuAndBufIt = mBuffers.begin(); cpuAndBufIt != mBuffers.end();) {
        const int cpu = cpuAndBufIt->first;
        void * const buf = cpuAndBufIt->second.buffer;

        // Take a snapshot of the positions
        struct perf_event_mmap_page *pemp = static_cast<struct perf_event_mmap_page *>(buf);
        const uint64_t head = ACCESS_ONCE(pemp->data_head);
        const uint64_t tail = ACCESS_ONCE(pemp->data_tail);

        if (head > tail) {
            const uint8_t * const b = static_cast<uint8_t *>(buf) + gSessionData.mPageSize;
            frame.add(cpu, head, tail, b);

            // Update tail with the data read
            pemp->data_tail = head;
        }

        auto discard = mDiscard.find(cpu);
        if (discard != mDiscard.end()) {
            munmap(buf, gSessionData.mPageSize + calculateMMapLength() - 1);
            mDiscard.erase(discard);
            logg.logMessage("Unmapped cpu %i", cpu);
            cpuAndBufIt = mBuffers.erase(cpuAndBufIt);
        }
        else {
            ++cpuAndBufIt;
        }
    }

    frame.send();

    return true;
}
