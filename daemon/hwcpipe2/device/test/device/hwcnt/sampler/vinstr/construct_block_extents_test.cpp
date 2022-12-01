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

#include <catch2/catch.hpp>

#include <device/hwcnt/block_extents.hpp>
#include <device/hwcnt/block_extents_operators.hpp>
#include <device/hwcnt/sampler/vinstr/construct_block_extents.hpp>

using namespace hwcpipe::device::hwcnt;

SCENARIO("device::hwcnt::sampler::vinstr::construct_block_extents", "[unit]") {
    uint64_t gpu_id = 0;
    uint16_t num_l2_slices = 0;
    uint16_t num_shader_cores = 0;

    /** 0x6956U == V4 Block Layout, 0x1001U == V5 Block Layout */
    std::tie(gpu_id, num_l2_slices, num_shader_cores) =
        GENERATE(std::make_tuple(0x6956U, 1, 4), std::make_tuple(0x1001U, 2, 10));

    hwcpipe::device::product_id pid{gpu_id};

    block_extents extents = sampler::vinstr::construct_block_extents(pid, num_l2_slices, num_shader_cores);

    block_extents expected_extents{
        {{
            1,                // num_fe_blocks
            1,                // num_tiler_blocks
            num_l2_slices,    // num_memory_block
            num_shader_cores, // num_core_blocks
        }},
        64,                         // counters_per_block
        sample_values_type::uint32, // values_type
    };

    CHECK(extents == expected_extents);
}
