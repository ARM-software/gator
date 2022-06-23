/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "BufferUtils.h"
#include "agents/perf/record_types.h"
#include "async/async_buffer.hpp"

#include <limits>

namespace agents::perf::encoders {

    /**
     * Instances of this class are capable of encoding a record from the Perf ring buffer
     * in the APC format and writing it into a preallocated buffer.
     */
    class data_record_apc_encoder_t {
    public:
        // TODO: the old PerfToMemoryBuffer code doesn't impose a limit on the
        // size of a PERF_DATA payload. Might need to revisit this as it won't
        // work with the async_buffer_t allocation limits
        static constexpr int max_payload_size = std::numeric_limits<int>::max();
        static constexpr int max_header_size = buffer_utils::MAXSIZE_PACK32 // frame type
                                             + buffer_utils::MAXSIZE_PACK32 // cpu
                                             + sizeof(std::uint32_t);       // blob length

        /**
         * Calculates the number of bytes of buffer space required to fully encode the
         * remainder of the data record.
         *
         * @param record The record that is to be processed.
         * @param offset_in_record The offset of the first unconsumed element in the record.
         * @return The number of bytes required to encode the record data.
         */
        static std::size_t get_bytes_required(const data_record_chunk_tuple_t & record, std::size_t offset_in_record);

        /**
         * Encode the contents of the record into the specified buffer. The buffer will be at most
         * max_header_size + max_payload_size bytes in length. If the encoded record won't fit into
         * that space a second call will be made to this method with a buffer for the remainder.
         *
         * @param buffer The preallocated buffer to copy into.
         * @param action Once the buffer has been populated either action.commit() or action.discard()
         *   should be called to indicate that the buffer has been used and should be passed to a
         *   consumer.
         * @param record The record to encode.
         * @param cpu The record was taken from this CPU's ring buffer.
         * @param tail_pointer The tail pointer of the CPU's ring buffer at the point this
         *   record was taken.
         * @param offset_in_record The index of the first unconsumed element in the record.
         *   Elements before this will have already been encoded by prior calls to encode_into.
         * @return An updated offset into the record of the next element to be encoded. In other
         *   words: offset_in_record + number of elements encoded by this call.
         */
        static std::size_t encode_into(async::async_buffer_t::mutable_buffer_type buffer,
                                       async::async_buffer_t::commit_action_t action,
                                       const data_record_chunk_tuple_t & record,
                                       int cpu,
                                       uint64_t tail_pointer,
                                       std::size_t offset_in_record);
    };

    /**
     * Instances of this class are capable of encoding a record from the Perf aux ring buffer
     * in the APC format and writing it into a preallocated buffer.
     */
    class aux_record_apc_encoder_t {
    public:
        static constexpr int max_header_size = buffer_utils::MAXSIZE_PACK32  // frame type
                                             + buffer_utils::MAXSIZE_PACK32  // cpu
                                             + buffer_utils::MAXSIZE_PACK64  // tail
                                             + buffer_utils::MAXSIZE_PACK32; // size
        static constexpr int max_payload_size = ISender::MAX_RESPONSE_LENGTH - max_header_size;

        static std::size_t get_bytes_required(const aux_record_chunk_t & record, std::size_t offset_in_record);

        static std::size_t encode_into(async::async_buffer_t::mutable_buffer_type buffer,
                                       async::async_buffer_t::commit_action_t action,
                                       const aux_record_chunk_t & record,
                                       int cpu,
                                       uint64_t tail_pointer,
                                       std::size_t offset_in_record);
    };

}
