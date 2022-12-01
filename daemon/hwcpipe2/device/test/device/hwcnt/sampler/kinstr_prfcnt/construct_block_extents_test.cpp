/*
 * Copyright (c) 2022 ARM Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "union_init.hpp"

#include <catch2/catch.hpp>

#include <device/hwcnt/block_extents.hpp>
#include <device/hwcnt/block_extents_operators.hpp>
#include <device/hwcnt/sampler/kinstr_prfcnt/construct_block_extents.hpp>
#include <device/hwcnt/sampler/kinstr_prfcnt/enum_info.hpp>

namespace kinstr_prfcnt = hwcpipe::device::ioctl::kinstr_prfcnt;

SCENARIO("device::hwcnt::sampler::kinstr::construct_block_extents", "[unit]") {
    using hwcpipe::device::hwcnt::block_extents;
    using hwcpipe::device::hwcnt::sampler::kinstr_prfcnt::enum_info;

    uint16_t counters_per_block = GENERATE(64, 128);
    uint8_t num_fe_blocks = GENERATE(1, 2, 3, 4);
    uint8_t num_tiler_blocks = GENERATE(1, 2, 3, 4);
    uint8_t num_memory_blocks = GENERATE(1, 2, 3, 4);
    uint8_t num_core_blocks = GENERATE(1, 2, 3, 4);

    enum_info ei{
        hwcpipe::device::hwcnt::prfcnt_set::primary,
        counters_per_block,
        {{{num_fe_blocks}, {num_tiler_blocks}, {num_memory_blocks}, {num_core_blocks}}},
        false,
        false,
    };

    block_extents extents = hwcpipe::device::hwcnt::sampler::kinstr_prfcnt::construct_block_extents(ei);

    block_extents expected_extents{
        {{
            num_fe_blocks,     // num_fe_blocks
            num_tiler_blocks,  // num_tiler_blocks
            num_memory_blocks, // num_memory_block
            num_core_blocks,   // num_core_blocks
        }},
        counters_per_block,                                 // counters_per_block
        hwcpipe::device::hwcnt::sample_values_type::uint64, // values_type
    };

    CHECK(extents == expected_extents);
}
