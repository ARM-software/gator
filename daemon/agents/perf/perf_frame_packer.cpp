/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#include "agents/perf/perf_frame_packer.hpp"

#include "ISender.h"
#include "agents/perf/async_buffer_builder.h"
#include "k/perf_event.h"

namespace agents::perf {

    namespace {
        using sample_word_type = std::uint64_t;

        constexpr std::size_t sample_word_size = sizeof(sample_word_type);

        constexpr std::size_t max_data_header_size = buffer_utils::MAXSIZE_PACK32 // frame type
                                                   + buffer_utils::MAXSIZE_PACK32 // cpu
                                                   + 4;                           // size
        constexpr std::size_t max_data_payload_size =
            std::min<std::size_t>(ISender::MAX_RESPONSE_LENGTH - max_data_header_size,
                                  1024UL * 1024UL); // limit frame size

        constexpr std::size_t max_aux_header_size = buffer_utils::MAXSIZE_PACK32  // frame type
                                                  + buffer_utils::MAXSIZE_PACK32  // cpu
                                                  + buffer_utils::MAXSIZE_PACK64  // tail
                                                  + buffer_utils::MAXSIZE_PACK32; // size
        constexpr std::size_t max_aux_payload_size =
            std::min<std::size_t>(ISender::MAX_RESPONSE_LENGTH - max_aux_header_size,
                                  1024UL * 1024UL); // limit frame size

        [[nodiscard]] bool append_data_record(apc_buffer_builder_t<std::vector<char>> & builder,
                                              lib::Span<sample_word_type const> data)
        {
            for (auto w : data) {
                builder.packInt64(w);
            }

            return builder.getWriteIndex() <= max_data_payload_size;
        }

        template<typename T>
        [[nodiscard]] T const * ring_buffer_ptr(char const * base, std::size_t position_masked)
        {
            return reinterpret_cast<const T *>(base + position_masked);
        }

        template<typename T>
        [[nodiscard]] T const * ring_buffer_ptr(char const * base, std::size_t position, std::size_t size_mask)
        {
            return ring_buffer_ptr<T>(base, position & size_mask);
        }

    }

    std::pair<std::uint64_t, std::vector<char>> extract_one_perf_data_apc_frame(
        int cpu,
        lib::Span<char const> data_mmap,
        std::uint64_t const header_head, // NOLINT(bugprone-easily-swappable-parameters)
        std::uint64_t const header_tail)
    {
        auto const buffer_mask = data_mmap.size() - 1; // assumes the size is a power of two (which it should be)

        // don't output an empty frame
        if (header_tail >= header_head) {
            return {header_tail, {}};
        }

        std::vector<char> buffer {};
        buffer.reserve(max_data_payload_size);
        apc_buffer_builder_t builder {buffer};

        // add the frame header
        builder.beginFrame(FrameType::PERF_DATA);
        builder.packInt(cpu);
        // skip the length field for now
        auto const length_index = builder.getWriteIndex();
        builder.advanceWrite(4);

        // accumulate one or more records to fit into some message
        auto current_tail = header_tail;
        while (current_tail < header_head) {
            auto const * record_header =
                ring_buffer_ptr<perf_event_header>(data_mmap.data(), current_tail, buffer_mask);
            auto const record_size =
                std::max<std::size_t>(8U, (record_header->size + sample_word_size - 1) & ~(sample_word_size - 1));
            auto const record_end = current_tail + record_size;
            std::size_t const base_masked = (current_tail & buffer_mask);
            std::size_t const end_masked = (record_end & buffer_mask);

            // incomplete or currently written record; is it possible? lets just be defensive
            if (record_end > header_head) {
                break;
            }

            auto const have_wrapped = end_masked < base_masked;

            std::size_t const first_size = (have_wrapped ? (data_mmap.size() - base_masked) : record_size);
            std::size_t const second_size = (have_wrapped ? end_masked : 0);

            // encode the chunk
            auto const current_offset = builder.getWriteIndex();

            LOG_TRACE("appending record %p (%zu -> %" PRIu64 ") (%zu / %zu / %u / %zu / %zu / %zu)",
                      record_header,
                      record_size,
                      record_end,
                      base_masked,
                      end_masked,
                      have_wrapped,
                      first_size,
                      second_size,
                      current_offset);

            if ((!append_data_record(builder,
                                     {
                                         ring_buffer_ptr<sample_word_type>(data_mmap.data(), base_masked),
                                         first_size / sample_word_size,
                                     }))
                || (!append_data_record(builder,
                                        {
                                            ring_buffer_ptr<sample_word_type>(data_mmap.data(), 0),
                                            second_size / sample_word_size,
                                        }))) {
                LOG_TRACE("... aborted");
                builder.trimTo(current_offset);
                break;
            }

            LOG_TRACE("current tail = %" PRIu64, record_end);

            // next
            current_tail = record_end;
        }

        // don't output an empty frame
        if (current_tail == header_tail) {
            return {header_tail, {}};
        }

        // now fill in the length field
        auto const bytes_written = builder.getWriteIndex() - (length_index + 4);
        LOG_TRACE("setting length = %zu", bytes_written);
        builder.writeLeUint32At(length_index, bytes_written);

        // commit the frame
        builder.endFrame();

        return {current_tail, std::move(buffer)};
    }

    std::pair<lib::Span<char const>, lib::Span<char const>> extract_one_perf_aux_apc_frame_data_span_pair(
        lib::Span<char const> aux_mmap,
        std::uint64_t const header_head,
        std::uint64_t const header_tail)
    {
        // ignore invalid / empty input
        if (header_head <= header_tail) {
            return {{}, {}};
        }

        const std::size_t buffer_mask = aux_mmap.size() - 1;

        // will be 'length' at most otherwise somehow wrapped many times
        const std::size_t total_data_size = std::min<std::uint64_t>(header_head - header_tail, //
                                                                    aux_mmap.size());
        const std::uint64_t head = header_head;

        // will either be the same as 'tail' or will be > if somehow wrapped multiple times
        const std::uint64_t tail = (header_head - total_data_size);

        const std::size_t tail_masked = (tail & buffer_mask);
        const std::size_t head_masked = (head & buffer_mask);

        const bool have_wrapped = head_masked < tail_masked;

        const std::size_t first_size = (have_wrapped ? (aux_mmap.size() - tail_masked) : total_data_size);
        const std::size_t second_size = (have_wrapped ? head_masked : 0);
        const std::size_t combined_size = first_size + second_size;

        if (first_size >= max_aux_payload_size) {
            // send just the first lot
            return {{aux_mmap.data() + tail_masked, max_aux_payload_size}, {}};
        }

        if (combined_size >= max_aux_payload_size) {
            auto const trimmed_second_size = max_aux_payload_size - first_size;
            // send both, but second is trimmed
            return {{aux_mmap.data() + tail_masked, first_size}, {aux_mmap.data(), trimmed_second_size}};
        }

        // send both, will fit in one message
        return {{aux_mmap.data() + tail_masked, first_size}, {aux_mmap.data(), second_size}};
    }

    std::pair<std::uint64_t, std::vector<char>> encode_one_perf_aux_apc_frame(int cpu,
                                                                              lib::Span<char const> first_span,
                                                                              lib::Span<char const> second_span,
                                                                              std::uint64_t const header_tail)
    {
        auto const combined_size = first_span.size() + second_span.size();

        // create the message data
        std::vector<char> buffer {};
        buffer.reserve(combined_size);

        apc_buffer_builder_t builder {buffer};

        builder.beginFrame(FrameType::PERF_AUX);
        builder.packInt(cpu);
        builder.packInt64(header_tail);
        builder.packIntSize(combined_size);
        builder.writeBytes(first_span.data(), first_span.size());
        builder.writeBytes(second_span.data(), second_span.size());
        builder.endFrame();

        return {header_tail + combined_size, std::move(buffer)};
    }
}
