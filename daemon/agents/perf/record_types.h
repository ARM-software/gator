/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include <cstddef>
#include <cstdint>

namespace agents::perf {

    struct buffer_config_t {
        /// must be power of 2
        size_t page_size;
        /// must be power of 2 multiple of pageSize
        size_t data_buffer_size;
        /// must be power of 2 multiple of pageSize (or 0)
        size_t aux_buffer_size;
    };

    using data_word_t = std::uint64_t;

    /**
     * A chunk of a perf aux record
     */
    struct aux_record_chunk_t {
        /** The pointer to the first byte of the record */
        const char * chunk_pointer;
        /** The number of bytes in the record */
        std::size_t byte_count;

        [[nodiscard]] std::size_t number_of_elements() const { return byte_count; }
    };

    /**
     * A chunk of a perf data record
     */
    struct data_record_chunk_t {
        /** The pointer to the first word of the record (where each word is a U64) */
        const data_word_t * chunk_pointer;
        /** The number of U64 words (not bytes) in the record */
        std::size_t word_count;
    };

    /**
     * A tuple of {@link data_record_chunk_t}s where the first chunk is required and the second is optional.
     * Each chunk specifies a sequence of words that make up the record.
     *
     * The second chunk is used when the record is split across the end of the ring-buffer. When it is
     * not used, it will have its length set to zero.
     */
    struct data_record_chunk_tuple_t {
        data_record_chunk_t first_chunk;
        data_record_chunk_t optional_second_chunk;

        [[nodiscard]] std::size_t number_of_elements() const
        {
            const auto optional_elems =
                optional_second_chunk.chunk_pointer == nullptr ? 0 : optional_second_chunk.word_count;
            return first_chunk.word_count + optional_elems;
        }
    };
}
