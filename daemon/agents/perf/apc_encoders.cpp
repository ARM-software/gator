/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#include "agents/perf/apc_encoders.h"

#include "Protocol.h"
#include "agents/perf/async_buffer_builder.h"
#include "agents/perf/record_types.h"

#include <cstdint>

namespace agents::perf::encoders {

    std::size_t data_record_apc_encoder_t::get_bytes_required(const data_record_chunk_tuple_t & record,
                                                              std::size_t offset_in_record)
    {
        return (record.number_of_elements() - offset_in_record) * buffer_utils::MAXSIZE_PACK64;
    }

    std::size_t data_record_apc_encoder_t::encode_into(async::async_buffer_t::mutable_buffer_type buffer,
                                                       async::async_buffer_t::commit_action_t action,
                                                       const data_record_chunk_tuple_t & record,
                                                       int cpu,
                                                       uint64_t /*tail_pointer*/,
                                                       std::size_t offset_in_record)
    {
        auto builder = async_buffer_builder_t(buffer, std::move(action));

        builder.beginFrame(FrameType::PERF_DATA);
        builder.packInt(cpu);
        // skip the length field for now
        const auto length_index = builder.getWriteIndex();
        builder.advanceWrite(4);

        std::size_t bytes_written = 0;

        auto offset = offset_in_record;

        // copy as much of the first record as we can
        const auto & first = record.first_chunk;
        for (; offset < first.word_count && builder.bytesAvailable() >= buffer_utils::MAXSIZE_PACK64; ++offset) {
            auto value = static_cast<int64_t>(first.chunk_pointer[offset]);
            bytes_written += builder.packInt64(value);
        }

        // if there's a second chunk, and we have space, start copying it
        auto second = record.optional_second_chunk;
        if (offset >= first.word_count && second.chunk_pointer != nullptr) {
            auto second_offset = offset - first.word_count;

            for (; second_offset < second.word_count && builder.bytesAvailable() >= buffer_utils::MAXSIZE_PACK64;
                 ++second_offset) {
                auto value = static_cast<int64_t>(second.chunk_pointer[second_offset]);
                bytes_written += builder.packInt64(value);
            }

            offset = second_offset + first.word_count;
        }

        // now fill in the length field
        const std::array<char, sizeof(std::uint32_t)> length_buffer {char(bytes_written),
                                                                     char(bytes_written >> 8),
                                                                     char(bytes_written >> 16),
                                                                     char(bytes_written >> 24)};
        builder.writeDirect(length_index, length_buffer.data(), length_buffer.size());

        // commit the frame
        builder.endFrame();

        // return the offset of the first data word that we didn't manage to consume.
        // the next iteration will pick up from here
        return offset;
    }

    std::size_t aux_record_apc_encoder_t::get_bytes_required(const aux_record_chunk_t & record,
                                                             std::size_t offset_in_record)
    {
        return (record.number_of_elements() - offset_in_record) * buffer_utils::MAXSIZE_PACK64;
    }

    std::size_t aux_record_apc_encoder_t::encode_into(async::async_buffer_t::mutable_buffer_type buffer,
                                                      async::async_buffer_t::commit_action_t action,
                                                      const aux_record_chunk_t & record,
                                                      int cpu,
                                                      uint64_t tail_pointer,
                                                      std::size_t offset_in_record)
    {
        auto builder = async_buffer_builder_t(buffer, std::move(action));

        // after the header, how many bytes of the record can we fit into the buffer?
        const auto bytes_left_in_record = record.byte_count - offset_in_record;
        const auto num_bytes_to_copy = std::min(bytes_left_in_record, buffer.size() - max_header_size);

        builder.beginFrame(FrameType::PERF_AUX);
        builder.packInt(cpu);
        builder.packInt64(static_cast<std::int64_t>(tail_pointer));
        builder.packInt(static_cast<std::int32_t>(num_bytes_to_copy));
        builder.writeBytes(record.chunk_pointer + offset_in_record, num_bytes_to_copy);
        builder.endFrame();

        // return the new offset so we can pick up from this point on the next iteration
        return offset_in_record + num_bytes_to_copy;
    }

}
