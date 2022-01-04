/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfBuffer.h"

#include "BufferUtils.h"
#include "ISender.h"
#include "Logging.h"
#include "Protocol.h"
#include "k/perf_event.h"
#include "lib/Syscall.h"

#include <cerrno>
#include <cinttypes>
#include <climits>
#include <cstring>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

template<typename T>
static T readOnceAtomicRelaxed(const T & val)
{
    return __atomic_load_n(static_cast<const volatile T *>(&val), __ATOMIC_RELAXED);
}

void validate(const PerfBuffer::Config & config)
{
    if (((config.pageSize - 1) & config.pageSize) != 0) {
        LOG_ERROR("PerfBuffer::Config.pageSize (%zu) must be a power of 2", config.pageSize);
        handleException();
    }
    if (((config.dataBufferSize - 1) & config.dataBufferSize) != 0) {
        LOG_ERROR("PerfBuffer::Config.dataBufferSize (%zu) must be a power of 2", config.dataBufferSize);
        handleException();
    }
    if (config.dataBufferSize < config.pageSize) {
        LOG_ERROR("PerfBuffer::Config.dataBufferSize (%zu) must be a multiple of PerfBuffer::Config.pageSize (%zu)",
                  config.dataBufferSize,
                  config.pageSize);
        handleException();
    }

    if (((config.auxBufferSize - 1) & config.auxBufferSize) != 0) {
        LOG_ERROR("PerfBuffer::Config.auxBufferSize (%zu) must be a power of 2", config.auxBufferSize);
        handleException();
    }
    if ((config.auxBufferSize < config.pageSize) && (config.auxBufferSize != 0)) {
        LOG_ERROR("PerfBuffer::Config.auxBufferSize (%zu) must be a multiple of PerfBuffer::Config.pageSize (%zu)",
                  config.auxBufferSize,
                  config.pageSize);
        handleException();
    }
}

static std::size_t getDataMMapLength(const PerfBuffer::Config & config)
{
    return config.pageSize + config.dataBufferSize;
}

std::size_t PerfBuffer::getDataBufferLength() const
{
    return mConfig.dataBufferSize;
}

std::size_t PerfBuffer::getAuxBufferLength() const
{
    return mConfig.auxBufferSize;
}

PerfBuffer::PerfBuffer(PerfBuffer::Config config) : mConfig(config)
{
    validate(mConfig);
}

PerfBuffer::~PerfBuffer()
{
    for (auto cpuAndBuf : mBuffers) {
        lib::munmap(cpuAndBuf.second.data_buffer, getDataMMapLength(mConfig));
        if (cpuAndBuf.second.aux_buffer != nullptr) {
            lib::munmap(cpuAndBuf.second.aux_buffer, getAuxBufferLength());
        }
    }
}

bool PerfBuffer::useFd(const int fd, int cpu, bool collectAuxTrace)
{
    auto mmap = [this, cpu](size_t length, size_t offset, int fd) {
        void * const buf = lib::mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);

        if (buf == MAP_FAILED) {
            LOG_DEBUG("mmap failed for fd %i (errno=%d, %s, mmapLength=%zu, offset=%zu)",
                      fd,
                      errno,
                      strerror(errno),
                      length,
                      offset);
            if ((errno == ENOMEM) || ((errno == EPERM) && (geteuid() != 0))) {
                LOG_ERROR("Could not mmap perf buffer on cpu %d, '%s' (errno: %d) returned.\n"
                          "This may be caused by too small limit in /proc/sys/kernel/perf_event_mlock_kb\n"
                          "Try again with a smaller value of --mmap-pages\n"
                          "Usually a value of ((perf_event_mlock_kb * 1024 / page_size) - 1) or lower will work.\n"
                          "The current value effective value for --mmap-pages is %zu",
                          cpu,
                          strerror(errno),
                          errno,
                          mConfig.dataBufferSize / mConfig.pageSize);
            }
        }
        return buf;
    };

    auto buffer = mBuffers.find(cpu);
    if (buffer != mBuffers.end()) {
        if (lib::ioctl(fd, PERF_EVENT_IOC_SET_OUTPUT, buffer->second.fd) < 0) {
            LOG_DEBUG("ioctl failed for fd %i (errno=%d, %s)", fd, errno, strerror(errno));
            return false;
        }
    }
    else {
        void * buf = mmap(getDataMMapLength(mConfig), 0, fd);
        if (buf == MAP_FAILED) {
            return false;
        }

        mBuffers[cpu] = Buffer {buf, nullptr, fd, -1};

        struct perf_event_mmap_page & pemp = *static_cast<struct perf_event_mmap_page *>(buf);
        // Check the version
        const uint32_t compat_version = pemp.compat_version;
        if (compat_version != 0) {
            LOG_DEBUG("Incompatible perf_event_mmap_page compat_version (%i) for fd %i", compat_version, fd);
            return false;
        }
    }

    if (collectAuxTrace) {
        auto & buffer = mBuffers[cpu];
        if (buffer.aux_buffer == nullptr) {
            const size_t offset = getDataMMapLength(mConfig);
            const size_t length = getAuxBufferLength();

            struct perf_event_mmap_page & pemp = *static_cast<struct perf_event_mmap_page *>(buffer.data_buffer);
            pemp.aux_offset = offset;
            pemp.aux_size = length;

            void * buf = mmap(length, offset, fd);
            if (buf == MAP_FAILED) {
                return false;
            }

            buffer.aux_buffer = buf;
            if (buffer.aux_fd >= 0) {
                LOG_DEBUG("Multiple aux fds");
                return false;
            }
            buffer.aux_fd = fd;
        }
    }

    return true;
}

void PerfBuffer::discard(int cpu)
{
    auto it = mBuffers.find(cpu);
    if (it != mBuffers.end()) {
        it->second.aux_fd = -1;
        mDiscard.insert(cpu);
    }
}

bool PerfBuffer::isFull()
{
    for (auto cpuAndBuf : mBuffers) {
        // Take a snapshot of the positions
        auto * pemp = static_cast<struct perf_event_mmap_page *>(cpuAndBuf.second.data_buffer);
        const uint64_t dataHead = readOnceAtomicRelaxed(pemp->data_head);

        if ((dataHead + 2000) >= getDataBufferLength()) {
            return true;
        }

        if (cpuAndBuf.second.aux_buffer != nullptr) {
            const uint64_t auxHead = readOnceAtomicRelaxed(pemp->aux_head);
            if ((auxHead + 2000) >= getAuxBufferLength()) {
                return true;
            }
        }
    }

    return false;
}

static void sendAuxFrame(IPerfBufferConsumer & bufferConsumer,
                         int cpu,
                         uint64_t headerTail,
                         uint64_t headerHead,
                         const char * buffer,
                         std::size_t length)
{
    const std::size_t bufferMask = length - 1;

    // will be 'length' at most otherwise somehow wrapped many times
    const std::size_t totalDataSize = std::min<uint64_t>(headerHead - headerTail, length);
    const std::uint64_t head = headerHead;
    // will either be the same as 'tail' or will be > if somehow wrapped multiple times
    const std::uint64_t tail = (headerHead - totalDataSize);

    const std::size_t tailMasked = (tail & bufferMask);
    const std::size_t headMasked = (head & bufferMask);

    const bool haveWrapped = headMasked < tailMasked;

    const std::size_t firstSize = (haveWrapped ? (length - tailMasked) : totalDataSize);
    const std::size_t secondSize = (haveWrapped ? headMasked : 0);

    const IPerfBufferConsumer::AuxRecordChunk chunks[2] = {{buffer + tailMasked, firstSize}, {buffer, secondSize}};

    bufferConsumer.consumePerfAuxRecord(cpu, tail, chunks);
}

template<typename T>
static inline const T * ringBufferPtr(const char * base, std::size_t positionMasked)
{
    return reinterpret_cast<const T *>(base + positionMasked);
}

template<typename T>
static inline const T * ringBufferPtr(const char * base, std::uint64_t position, std::size_t sizeMask)
{
    return ringBufferPtr<T>(base, (position & sizeMask));
}

static void sendDataFrame(IPerfBufferConsumer & bufferConsumer,
                          int cpu,
                          uint64_t head,
                          uint64_t tail,
                          const char * b,
                          std::size_t length)
{
    static constexpr std::size_t CHUNK_BUFFER_SIZE = 256; // arbitrary, roughly 4k size stack allocation on 64-bit
    static constexpr std::size_t CHUNK_WORD_SIZE = sizeof(IPerfBufferConsumer::data_word_t);

    const std::size_t bufferMask = length - 1;

    std::size_t numChunksInBuffer = 0;
    IPerfBufferConsumer::DataRecordChunkTuple chunkBuffer[CHUNK_BUFFER_SIZE];

    while (head > tail) {
        // write the chunks we have so far, so we can reuse the buffer
        if (numChunksInBuffer == CHUNK_BUFFER_SIZE) {
            bufferConsumer.consumePerfDataRecord(cpu, {chunkBuffer, numChunksInBuffer});
            numChunksInBuffer = 0;
        }

        // create the next chunk
        const auto * recordHeader = ringBufferPtr<perf_event_header>(b, tail, bufferMask);
        const auto recordSize = (recordHeader->size + CHUNK_WORD_SIZE - 1) & ~(CHUNK_WORD_SIZE - 1);
        const auto recordEnd = tail + recordSize;
        const std::size_t baseMasked = (tail & bufferMask);
        const std::size_t endMasked = (recordEnd & bufferMask);

        const bool haveWrapped = endMasked < baseMasked;

        const std::size_t firstSize = (haveWrapped ? (length - baseMasked) : recordSize);
        const std::size_t secondSize = (haveWrapped ? endMasked : 0);

        // set chunk
        chunkBuffer[numChunksInBuffer].firstChunk.chunkPointer =
            ringBufferPtr<IPerfBufferConsumer::data_word_t>(b, baseMasked);
        chunkBuffer[numChunksInBuffer].firstChunk.wordCount = firstSize / CHUNK_WORD_SIZE;
        chunkBuffer[numChunksInBuffer].optionalSecondChunk.chunkPointer =
            ringBufferPtr<IPerfBufferConsumer::data_word_t>(b, 0);
        chunkBuffer[numChunksInBuffer].optionalSecondChunk.wordCount = secondSize / CHUNK_WORD_SIZE;

        numChunksInBuffer += 1;
        tail = recordEnd;
    }

    // write the remaining chunks
    if (numChunksInBuffer > 0) {
        bufferConsumer.consumePerfDataRecord(cpu, {chunkBuffer, numChunksInBuffer});
    }
}

bool PerfBuffer::send(IPerfBufferConsumer & bufferConsumer)
{
    const std::size_t dataBufferLength = getDataBufferLength();
    const std::size_t auxBufferLength = getAuxBufferLength();

    for (auto cpuAndBufIt = mBuffers.begin(); cpuAndBufIt != mBuffers.end();) {
        const int cpu = cpuAndBufIt->first;

        // Take a snapshot of the data buffer positions
        // We read the data buffer positions before we read the aux buffer positions
        // so that we never send records more recent than the aux
        void * const dataBuf = cpuAndBufIt->second.data_buffer;
        auto * pemp = static_cast<struct perf_event_mmap_page *>(dataBuf);
        const uint64_t dataHead = __atomic_load_n(&pemp->data_head, __ATOMIC_ACQUIRE);
        // Only we write this so no atomic load needed
        const uint64_t dataTail = pemp->data_tail;

        auto discard = mDiscard.find(cpu);
        const bool shouldDiscard = (discard != mDiscard.end());

        // Now send the aux data before the records to ensure the consumer never receives
        // a PERF_RECORD_AUX without already having received the aux data
        void * const auxBuf = cpuAndBufIt->second.aux_buffer;
        if (auxBuf != nullptr) {
            const uint64_t auxHead = __atomic_load_n(&pemp->aux_head, __ATOMIC_ACQUIRE);
            // Only we write this so no atomic load needed
            const uint64_t auxTail = pemp->aux_tail;

            if (auxHead > auxTail) {
                const char * const b = static_cast<char *>(auxBuf);

                sendAuxFrame(bufferConsumer, cpu, auxTail, auxHead, b, auxBufferLength);

                // Update tail with the aux read and synchronize with the buffer writer
                __atomic_store_n(&pemp->aux_tail, auxHead, __ATOMIC_RELEASE);

                // The AUX buffer event will be disabled if the AUX buffer fills before we read it.
                // Since we cannot easily tell that without parsing the data MMAP (which we currently don't do)
                // Just call enable again here after updating the tail pointer. That way, if the event was
                // disabled, it will be reenabled now so more data can be received
                if ((!shouldDiscard) && (cpuAndBufIt->second.aux_fd >= 0)) {
                    if (lib::ioctl(cpuAndBufIt->second.aux_fd, PERF_EVENT_IOC_ENABLE, 0) != 0) {
                        LOG_ERROR("Unable to enable a perf event");
                    }
                }
            }
        }

        if (dataHead > dataTail) {
            const char * const b = static_cast<char *>(dataBuf) + mConfig.pageSize;

            sendDataFrame(bufferConsumer, cpu, dataHead, dataTail, b, dataBufferLength);

            // Update tail with the data read and synchronize with the buffer writer
            __atomic_store_n(&pemp->data_tail, dataHead, __ATOMIC_RELEASE);
        }

        if (shouldDiscard) {
            lib::munmap(dataBuf, getDataMMapLength(mConfig));
            if (auxBuf != nullptr) {
                lib::munmap(auxBuf, auxBufferLength);
            }
            mDiscard.erase(discard);
            LOG_DEBUG("Unmapped cpu %i", cpu);
            cpuAndBufIt = mBuffers.erase(cpuAndBufIt);
        }
        else {
            ++cpuAndBufIt;
        }
    }

    return true;
}
