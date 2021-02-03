/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#pragma once

#include "Config.h"
#include "lib/Span.h"

#include <map>
#include <set>
#include <vector>

class IPerfBufferConsumer {
public:
    virtual ~IPerfBufferConsumer() = default;

    using data_word_t = std::uint64_t;

    /**
     * A chunk of a perf aux record
     */
    struct AuxRecordChunk {
        /** The pointer to the first byte of the record */
        const char * chunkPointer;
        /** The number of bytes in the record */
        std::size_t byteCount;
    };

    /**
     * A chunk of a perf data record
     */
    struct DataRecordChunk {
        /** The pointer to the first word of the record (where each word is a U64) */
        const data_word_t * chunkPointer;
        /** The number of U64 words (not bytes) in the record */
        std::size_t wordCount;
    };

    /**
     * A tuple of {@link DataRecordChunk}s where the first chunk is required and the second is optional.
     * Each chunk specifies a sequence of words that make up the record.
     *
     * The second chunk is used when the record is split across the end of the ring-buffer. When it is
     * not used, it will have its length set to zero.
     */
    struct DataRecordChunkTuple {
        DataRecordChunk firstChunk;
        DataRecordChunk optionalSecondChunk;
    };

    /**
     * Consume a chunk of aux data
     *
     * @param cpu The CPU the data came from
     * @param auxTailValue The Initial 'tail' value for the aux data
     * @param recordChunks The span of chunks that contains the data
     */
    virtual void consumePerfAuxRecord(int cpu,
                                      std::uint64_t auxTailValue,
                                      lib::Span<const AuxRecordChunk> recordChunks) = 0;

    /**
     * Consume a sequence of perf data record chunks
     *
     * @param cpu The CPU the records came from
     * @param recordChunks The sequence of chunk-tuples
     */
    virtual void consumePerfDataRecord(int cpu, lib::Span<const DataRecordChunkTuple> recordChunks) = 0;
};
