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
#include <limits.h>
#include <errno.h>
#include "k/perf_event.h"

#include "lib/Syscall.h"

#include "BufferUtils.h"
#include "Logging.h"
#include "ISender.h"
#include "Protocol.h"

template<typename T>
static T readOnceAtomicRelaxed(const T & val) {
    return __atomic_load_n(static_cast<const volatile T *>(&val), __ATOMIC_RELAXED);
}

void validate(const PerfBuffer::Config & config)
{
    if (((config.pageSize - 1) & config.pageSize) != 0) {
        logg.logError("PerfBuffer::Config.pageSize (%zu) must be a power of 2", config.pageSize);
        handleException();
    }
    if (((config.bufferSize - 1) & config.bufferSize) != 0) {
        logg.logError("PerfBuffer::Config.bufferSize (%zu) must be a power of 2", config.bufferSize);
        handleException();
    }
    if (config.bufferSize < config.pageSize) {
        logg.logError("PerfBuffer::Config.bufferSize (%zu) must be a multiple of PerfBuffer::Config.pageSize (%zu)", config.bufferSize, config.pageSize);
        handleException();
    }
}

static std::size_t calculateMMapLength(const PerfBuffer::Config & config)
{
    return config.pageSize + config.bufferSize;
}

std::size_t PerfBuffer::calculateBufferLength() const
{
    return mConfig.bufferSize;
}

PerfBuffer::PerfBuffer(PerfBuffer::Config config)
        : mConfig(config),
          mBuffers(),
          mDiscard()
{
    validate(mConfig);
}

PerfBuffer::~PerfBuffer()
{
    for (auto cpuAndBuf : mBuffers) {
        lib::munmap(cpuAndBuf.second.data_buffer, calculateMMapLength(mConfig));
        if (cpuAndBuf.second.aux_buffer != nullptr)
            lib::munmap(cpuAndBuf.second.aux_buffer, calculateBufferLength());
    }
}

bool PerfBuffer::useFd(const int fd, int cpu, bool collectAuxTrace)
{
    auto mmap = [this, cpu](size_t length, size_t offset, int fd) {
        void * const buf = lib::mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);

        if (buf == MAP_FAILED) {
            logg.logMessage("mmap failed for fd %i (errno=%d, %s, mmapLength=%zu, offset=%zu)", fd, errno, strerror(errno), length, offset);
            if ((errno == EPERM) && (geteuid() != 0)) {
                logg.logError("Could not mmap perf buffer on cpu %d, EPERM returned.\n"
                        "This may be caused by too small limit in /proc/sys/kernel/perf_event_mlock_kb\n"
                        "Try again with a smaller value of --mmap-pages\n"
                        "Usually a value of ((perf_event_mlock_kb * 1024 / page_size) - 1) or lower will work.\n"
                        "The current value effective value for --mmap-pages is %zu", cpu, mConfig.bufferSize / mConfig.pageSize);
            }
        }
        return buf;
    };

    auto buffer = mBuffers.find(cpu);
    if (buffer != mBuffers.end()) {
        if (lib::ioctl(fd, PERF_EVENT_IOC_SET_OUTPUT, buffer->second.fd) < 0) {
            logg.logMessage("ioctl failed for fd %i (errno=%d, %s)", fd, errno, strerror(errno));
            return false;
        }
    }
    else {
        void * buf = mmap(calculateMMapLength(mConfig), 0, fd);
        if (buf == MAP_FAILED)
            return false;

        mBuffers[cpu] = Buffer { fd, buf, nullptr };

        struct perf_event_mmap_page & pemp = *static_cast<struct perf_event_mmap_page *>(buf);
        // Check the version
        const uint32_t compat_version = pemp.compat_version;
        if (compat_version != 0) {
            logg.logMessage("Incompatible perf_event_mmap_page compat_version (%i) for fd %i", compat_version, fd);
            return false;
        }
    }

    if (collectAuxTrace) {
        auto & buffer = mBuffers[cpu];
        if (buffer.aux_buffer == nullptr) {
            const size_t offset = calculateMMapLength(mConfig);
            const size_t length = calculateBufferLength();

            struct perf_event_mmap_page & pemp = *static_cast<struct perf_event_mmap_page *>(buffer.data_buffer);
            pemp.aux_offset = offset;
            pemp.aux_size = length;

            void * buf = mmap(length, offset, fd);
            if (buf == MAP_FAILED)
                return false;

            buffer.aux_buffer = buf;
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
        struct perf_event_mmap_page *pemp = static_cast<struct perf_event_mmap_page *>(cpuAndBuf.second.data_buffer);
        const uint64_t tail = readOnceAtomicRelaxed(pemp->data_tail);
        const uint64_t head = readOnceAtomicRelaxed(pemp->data_head);

        if (head != tail) {
            return false;
        }
        if (cpuAndBuf.second.aux_buffer != nullptr
                && readOnceAtomicRelaxed(pemp->aux_tail) != readOnceAtomicRelaxed(pemp->aux_head))
        {
            return false;
        }
    }

    return true;
}

bool PerfBuffer::isFull()
{
    for (auto cpuAndBuf : mBuffers) {
        // Take a snapshot of the positions
        struct perf_event_mmap_page *pemp = static_cast<struct perf_event_mmap_page *>(cpuAndBuf.second.data_buffer);
        const uint64_t head = readOnceAtomicRelaxed(pemp->data_head);

        if ((head + 2000) <= calculateBufferLength()) {
            return true;
        }
    }

    return false;
}

class PerfDataFrame
{
public:
    PerfDataFrame(ISender & sender)
            : mSender(sender),
              mWritePos(-1),
              mCpuSizePos(-1)
    {
    }

    void add(const int cpu, uint64_t head, uint64_t tail, const char * b, std::size_t length)
    {
        cpuHeader(cpu);

        const std::size_t bufferMask = length - 1;

        while (head > tail) {
            const int count = reinterpret_cast<const struct perf_event_header *>(b + (tail & bufferMask))->size
                    / sizeof(uint64_t);
            // Can this whole message be written as Streamline assumes events are not split between frames
            if (sizeof(mBuf) <= mWritePos + count * buffer_utils::MAXSIZE_PACK64) {
                send();
                cpuHeader(cpu);
            }
            for (int i = 0; i < count; ++i) {
                // Must account for message size
                buffer_utils::packInt64(mBuf, mWritePos, *reinterpret_cast<const uint64_t *>(b + (tail & bufferMask)));
                tail += sizeof(uint64_t);
            }
        }
    }

    void send()
    {
        if (mWritePos > 0) {
            writeCpuSize();
            mSender.writeData(mBuf, mWritePos, ResponseType::APC_DATA);
            mWritePos = -1;
            mCpuSizePos = -1;
        }
    }

private:

    void frameHeader()
    {
        if (mWritePos < 0) {
            mWritePos = 0;
            mCpuSizePos = -1;
            buffer_utils::packInt(mBuf, mWritePos, static_cast<uint32_t>(FrameType::PERF_DATA));
        }
    }

    void writeCpuSize()
    {
        if (mCpuSizePos >= 0) {
            buffer_utils::writeLEInt(mBuf + mCpuSizePos,
                               mWritePos - mCpuSizePos - sizeof(uint32_t));
        }
    }

    void cpuHeader(const int cpu)
    {
        if (sizeof(mBuf) <= mWritePos + buffer_utils::MAXSIZE_PACK32 + sizeof(uint32_t)) {
            send();
        }
        frameHeader();
        writeCpuSize();
        buffer_utils::packInt(mBuf, mWritePos, cpu);
        mCpuSizePos = mWritePos;
        // Reserve space for cpu size
        mWritePos += sizeof(uint32_t);
    }

    // Pick a big size but something smaller than the chunkSize in Sender::writeData which is 100k
    char mBuf[1 << 16];
    ISender & mSender;
    int mWritePos;
    int mCpuSizePos;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(PerfDataFrame);
};

static void sendAuxFrame(ISender & sender, int cpu, uint64_t tail, uint64_t head, const char * buffer, std::size_t length)
{
    const std::size_t bufferMask = length - 1;
    constexpr std::size_t maxHeaderSize = buffer_utils::MAXSIZE_PACK32 // frame type
            + buffer_utils::MAXSIZE_PACK32 // cpu
            + buffer_utils::MAXSIZE_PACK64 // tail
            + buffer_utils::MAXSIZE_PACK32; // size

    while (tail < head) {
        // frame size must fit in int
        const uint64_t thisHead = std::min(tail + ISender::MAX_RESPONSE_LENGTH - maxHeaderSize, head);
        const int size = thisHead - tail;

        const std::size_t tailMasked = tail & bufferMask;
        const std::size_t headMasked = thisHead & bufferMask;

        const bool haveWrapped = headMasked < tailMasked;

        const int firstSize = haveWrapped ? length - tailMasked : size;
        const int secondSize = haveWrapped ? headMasked : 0;

        char header[maxHeaderSize];
        int pos = 0;
        buffer_utils::packInt(header, pos, static_cast<uint32_t>(FrameType::PERF_AUX));
        buffer_utils::packInt(header, pos, cpu);
        buffer_utils::packInt64(header, pos, tail);
        buffer_utils::packInt(header, pos, size);

        constexpr std::size_t numberOfParts = 3;
        const lib::Span<const char, int> parts[numberOfParts] = { { header, pos }, { buffer + tailMasked, firstSize }, {
                buffer, secondSize } };
        sender.writeDataParts( { parts, numberOfParts }, ResponseType::APC_DATA);
        tail = thisHead;
    }
}

bool PerfBuffer::send(ISender & sender)
{
    PerfDataFrame frame(sender);

    const std::size_t bufferLength = calculateBufferLength();

    for (auto cpuAndBufIt = mBuffers.begin(); cpuAndBufIt != mBuffers.end();) {
        const int cpu = cpuAndBufIt->first;

        // Take a snapshot of the data buffer positions
        // We read the data buffer positions before we read the aux buffer positions
        // so that we never send records more recent than the aux
        void * const dataBuf = cpuAndBufIt->second.data_buffer;
        struct perf_event_mmap_page *pemp = static_cast<struct perf_event_mmap_page *>(dataBuf);
        const uint64_t dataHead = __atomic_load_n(&pemp->data_head, __ATOMIC_ACQUIRE);
        // Only we write this so no atomic load needed
        const uint64_t dataTail = pemp->data_tail;

        // Now send the aux data before the records to ensure the consumer never receives
        // a PERF_RECORD_AUX without already having received the aux data
        void * const auxBuf = cpuAndBufIt->second.aux_buffer;
        if (auxBuf != nullptr)
        {
            const uint64_t auxHead = __atomic_load_n(&pemp->aux_head, __ATOMIC_ACQUIRE);
            // Only we write this so no atomic load needed
            const uint64_t auxTail = pemp->aux_tail;

            if (auxHead > auxTail) {
                const char * const b = static_cast<char *>(auxBuf);

                sendAuxFrame(sender, cpu, auxTail, auxHead, b, bufferLength);

                // Update tail with the aux read and synchronize with the buffer writer
                __atomic_store_n(&pemp->aux_tail, auxHead, __ATOMIC_RELEASE);
            }
        }

        if (dataHead > dataTail) {
            const char * const b = static_cast<char *>(dataBuf) + mConfig.pageSize;

            frame.add(cpu, dataHead, dataTail, b, bufferLength);

            // Update tail with the data read and synchronize with the buffer writer
            __atomic_store_n(&pemp->data_tail, dataHead, __ATOMIC_RELEASE);
        }

        auto discard = mDiscard.find(cpu);
        if (discard != mDiscard.end()) {
            lib::munmap(dataBuf, calculateMMapLength(mConfig));
            if (auxBuf != nullptr)
                lib::munmap(auxBuf, calculateBufferLength());
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
