/* Copyright (C) 2020-2021 by Arm Limited. All rights reserved. */

#pragma once

#include "Buffer.h"
#include "IBufferControl.h"
#include "IRawFrameBuilder.h"
#include "linux/perf/IPerfBufferConsumer.h"

#include <atomic>

#include <semaphore.h>

class PerfToMemoryBuffer : public IPerfBufferConsumer, public IBufferControl {
public:
    PerfToMemoryBuffer(Buffer & buffer, bool oneShot) : PerfToMemoryBuffer(buffer, buffer, oneShot) {}
    PerfToMemoryBuffer(IRawFrameBuilderWithDirectAccess & builder, IBufferControl & controller, bool oneShot);

    void consumePerfAuxRecord(int cpu,
                              std::uint64_t auxTailValue,
                              lib::Span<const AuxRecordChunk> recordChunks) override;
    void consumePerfDataRecord(int cpu, lib::Span<const DataRecordChunkTuple> recordChunks) override;

    bool write(ISender & sender) override;
    bool isFull() const override;
    void setDone() override;

private:
    IRawFrameBuilderWithDirectAccess & builder;
    IBufferControl & controller;
    sem_t bufferSem;
    std::atomic<bool> full;
    std::atomic<bool> done;
    bool oneShot;

    bool waitFor(std::size_t bytes);
    void endDataFrame(int lengthWriteIndex, std::uint32_t totalWrittenSinceFrameEnd);
    std::uint32_t appendData(const DataRecordChunkTuple & recordChunk);
    std::uint32_t appendData(const DataRecordChunk & recordChunk);
};
