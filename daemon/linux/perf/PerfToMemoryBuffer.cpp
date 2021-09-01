/* Copyright (C) 2020-2021 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfToMemoryBuffer.h"

#include "BufferUtils.h"
#include "ISender.h"
#include "lib/Assert.h"

PerfToMemoryBuffer::PerfToMemoryBuffer(IRawFrameBuilderWithDirectAccess & builder,
                                       IBufferControl & controller,
                                       bool oneShot)
    : builder(builder), controller(controller), bufferSem(), full(false), done(false), oneShot(oneShot)
{
    sem_init(&bufferSem, 0, 0);
}

bool PerfToMemoryBuffer::waitFor(std::size_t bytes)
{
    while (std::size_t(builder.bytesAvailable()) <= bytes) {
        if (oneShot || done) {
            full = true;
            return false;
        }
        builder.flush();
        sem_wait(&bufferSem);
    }
    return true;
}

bool PerfToMemoryBuffer::isFull() const
{
    return full || controller.isFull();
}

void PerfToMemoryBuffer::setDone()
{
    controller.setDone();
    done = true;
    sem_post(&bufferSem);
}

bool PerfToMemoryBuffer::write(ISender & sender)
{
    const auto result = controller.write(sender);
    sem_post(&bufferSem);
    return result;
}

void PerfToMemoryBuffer::consumePerfAuxRecord(int cpu,
                                              std::uint64_t auxTailValue,
                                              lib::Span<const AuxRecordChunk> recordChunks)
{
    static constexpr int MAX_HEADER_SIZE = buffer_utils::MAXSIZE_PACK32    // frame type
                                           + buffer_utils::MAXSIZE_PACK32  // cpu
                                           + buffer_utils::MAXSIZE_PACK64  // tail
                                           + buffer_utils::MAXSIZE_PACK32; // size
    static constexpr int MAX_FRAME_SIZE = ISender::MAX_RESPONSE_LENGTH - MAX_HEADER_SIZE;

    // skip if complete
    if (full) {
        return;
    }

    for (const auto & recordChunk : recordChunks) {
        for (std::size_t offset = 0; offset < recordChunk.byteCount;) {
            if (!waitFor(MAX_HEADER_SIZE)) {
                return;
            }

            const std::size_t bytesRemaining = recordChunk.byteCount - offset;
            const int maxWriteLength = std::min<std::size_t>(bytesRemaining, MAX_FRAME_SIZE);
            const int actualWriteLength = std::min<int>(maxWriteLength, builder.bytesAvailable() - MAX_HEADER_SIZE);

            if (actualWriteLength <= 0) {
                runtime_assert(actualWriteLength == 0, "Negative write length???");
                continue;
            }

            builder.beginFrame(FrameType::PERF_AUX);
            builder.packInt(cpu);
            builder.packInt64(auxTailValue);
            builder.packInt(actualWriteLength);
            builder.writeBytes(recordChunk.chunkPointer + offset, actualWriteLength);
            builder.endFrame();

            offset += actualWriteLength;
            auxTailValue += actualWriteLength;
        }
    }
}

void PerfToMemoryBuffer::consumePerfDataRecord(int cpu, lib::Span<const DataRecordChunkTuple> recordChunks)
{
    static constexpr int MAX_HEADER_SIZE = buffer_utils::MAXSIZE_PACK32   // frame type
                                           + buffer_utils::MAXSIZE_PACK32 // cpu
                                           + 4;                           // blob length

    // skip if complete
    if (full) {
        return;
    }

    static_assert(sizeof(IPerfBufferConsumer::data_word_t) == 8, "Expected word size is 64-bit");

    bool inFrame = false;
    int lengthWriteIndex = 0;
    std::uint32_t totalWrittenSinceFrameEnd = 0;
    for (const auto & recordChunk : recordChunks) {
        const std::size_t totalWordCount =
            recordChunk.firstChunk.wordCount +
            (recordChunk.optionalSecondChunk.chunkPointer != nullptr ? recordChunk.optionalSecondChunk.wordCount : 0);
        const std::size_t requiredBytesForRecord = totalWordCount * buffer_utils::MAXSIZE_PACK64;

        // are we in a frame, is there space to push another record?
        if (inFrame) {
            if (std::size_t(builder.bytesAvailable()) >= requiredBytesForRecord) {
                // yes, append the frame data and continue
                totalWrittenSinceFrameEnd += appendData(recordChunk);
                continue;
            }
            // no, just end the current frame
            endDataFrame(lengthWriteIndex, totalWrittenSinceFrameEnd);
            inFrame = false;
            totalWrittenSinceFrameEnd = 0;
        }

        const std::size_t totalRequiredBytes = MAX_HEADER_SIZE + requiredBytesForRecord;
        if (!waitFor(totalRequiredBytes)) {
            return;
        }

        // write the header
        builder.beginFrame(FrameType::PERF_DATA);
        builder.packInt(cpu);
        lengthWriteIndex = builder.getWriteIndex();
        builder.advanceWrite(4); // skip the length field for now

        // write the record
        inFrame = true;
        totalWrittenSinceFrameEnd = appendData(recordChunk);
    }

    if (inFrame) {
        endDataFrame(lengthWriteIndex, totalWrittenSinceFrameEnd);
    }
}

void PerfToMemoryBuffer::endDataFrame(int lengthWriteIndex, std::uint32_t totalWrittenSinceFrameEnd)
{
    const char lengthBuffer[4] = {char(totalWrittenSinceFrameEnd >> 0),
                                  char(totalWrittenSinceFrameEnd >> 8),
                                  char(totalWrittenSinceFrameEnd >> 16),
                                  char(totalWrittenSinceFrameEnd >> 24)};

    builder.writeDirect(lengthWriteIndex, lengthBuffer, 4);
    builder.endFrame();
}

std::uint32_t PerfToMemoryBuffer::appendData(const DataRecordChunkTuple & recordChunk)
{
    return appendData(recordChunk.firstChunk) + appendData(recordChunk.optionalSecondChunk);
}

std::uint32_t PerfToMemoryBuffer::appendData(const DataRecordChunk & recordChunk)
{
    std::uint32_t result = 0;

    if (recordChunk.chunkPointer != nullptr) {
        for (std::size_t index = 0; index < recordChunk.wordCount; ++index) {
            result += builder.packInt64(recordChunk.chunkPointer[index]);
        }
    }

    return result;
}
