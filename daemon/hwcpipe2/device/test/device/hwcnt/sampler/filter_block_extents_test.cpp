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

#include <device/hwcnt/block_extents_operators.hpp>
#include <device/hwcnt/sampler/filter_block_extents.hpp>
#include <device/mock/instance.hpp>

namespace hwcnt = hwcpipe::device::hwcnt;

TEST_CASE("device::hwcnt::sampler::block_extents_filter", "[unit]") {
    using hwcnt::sampler::block_extents_filter;
    using configuration_type = hwcnt::sampler::configuration;

    const auto values_type = GENERATE(hwcnt::sample_values_type::uint32, //
                                      hwcnt::sample_values_type::uint64);
    const auto counters_per_block = GENERATE(uint8_t{64}, uint8_t{128});
    const auto block_type = GENERATE(hwcnt::block_type::fe,     //
                                     hwcnt::block_type::tiler,  //
                                     hwcnt::block_type::memory, //
                                     hwcnt::block_type::core    //
    );

    hwcnt::block_extents instance_extents{
        {{
            1,
            1,
            1,
            1,
        }},
        counters_per_block,
        values_type,
    };

    mock::instance instance(instance_extents);

    SECTION("bad data") {
        std::array<configuration_type, 2> configuration{{{block_type}, {block_type}}};

        std::error_code ec;
        hwcnt::block_extents actual_extents{};
        std::tie(ec, actual_extents) = hwcnt::sampler::block_extents_filter::filter_block_extents(
            instance, configuration.begin(), configuration.end());

        CHECK(ec);
    }
    SECTION("good data") {
        std::array<configuration_type, 1> configuration{{{block_type}}};

        std::error_code ec;
        hwcnt::block_extents actual_extents{};
        std::tie(ec, actual_extents) = hwcnt::sampler::block_extents_filter::filter_block_extents(
            instance, configuration.begin(), configuration.end());

        hwcnt::block_extents::num_blocks_of_type_type num_blocks_of_type{};
        num_blocks_of_type[static_cast<size_t>(block_type)] = 1;

        hwcnt::block_extents expected{num_blocks_of_type, counters_per_block, values_type};

        REQUIRE(!ec);
        CHECK(actual_extents == expected);
    }
}
