/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "lib/Span.h"
#include "lib/error_code_or.hpp"

#include <cstdint>
#include <tuple>
#include <vector>

namespace agents::perf {

    /**
     * Given the current state of the perf data section of some mmap, extract some apc data frame from it
     *
     * @param cpu The cpu associated with the mmap
     * @param data_mmap The data area within the mmap
     * @param header_head The data_head value
     * @param header_tail The data_tail value
     * @return A pair, being the new value for data_tail, and the encoded apc_frame message
     */
    [[nodiscard]] std::pair<std::uint64_t, std::vector<char>> extract_one_perf_data_apc_frame(
        int cpu,
        lib::Span<char const> data_mmap,
        std::uint64_t header_head,
        std::uint64_t header_tail);

    /**
     * Given the current state of the perf aux section of some mmap, extract a pair of spans (pair to account for ringbuffer wrapping) representing
     * the chunk of raw aux data to send as part of some apc_frame message. The pair of spans will be sized such that the are no larger than the max sized
     * apc_frame payload. The pair of spans should be treated as one contiguous chunk of aux data (even though the two spans them selves may not be contiguous).
     *
     * @param aux_mmap The aux area within the mmap
     * @param header_head The aux_head value
     * @param header_tail The aux_tail value
     * @return A pair, being the first and second parts of the aux data chunk.
     */
    [[nodiscard]] std::pair<lib::Span<char const>, lib::Span<char const>> extract_one_perf_aux_apc_frame_data_span_pair(
        lib::Span<char const> aux_mmap,
        std::uint64_t header_head,
        std::uint64_t header_tail);

    /**
     * Given the pair of aux spans that were previously extracted by `extract_one_perf_aux_apc_frame_data_span_pair`,
     * encode them into an apc_frame message
     *
     * @param cpu The cpu associated with the mmap
     * @param first_span The first span returned by extract_one_perf_aux_apc_frame_data_span_pair
     * @param second_span The second span returned by extract_one_perf_aux_apc_frame_data_span_pair
     * @param header_tail The value of header_tail that was passed to extract_one_perf_aux_apc_frame_data_span_pair
     * @return A pair, being the new value for aux_tail, and the encoded apc_frame message
     */
    [[nodiscard]] std::pair<std::uint64_t, std::vector<char>> encode_one_perf_aux_apc_frame(
        int cpu,
        lib::Span<char const> first_span,
        lib::Span<char const> second_span,
        std::uint64_t header_tail);
}
