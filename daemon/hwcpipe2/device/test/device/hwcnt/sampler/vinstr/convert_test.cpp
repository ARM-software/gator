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

#include <device/hwcnt/sampler/vinstr/convert.hpp>
#include <device/ioctl/kbase/types.hpp>

#include <array>

/** @return 128 bit enable mask with `0b11110000` repeating pattern. */
static inline auto generate_huge_enable_mask() {
    using enable_map_type = hwcpipe::device::hwcnt::sampler::configuration::enable_map_type;

    enable_map_type result{};

    constexpr size_t pattern_len = 8;
    constexpr size_t pattern_len_half = pattern_len / 2;

    for (size_t i = 0; i < result.size(); ++i) {
        result[i] = (i % pattern_len) >= pattern_len_half;
    }

    return result;
}

TEST_CASE("device::hwcnt::sampler::vinstr::convert", "[unit]") {
    using hwcpipe::device::hwcnt::block_type;
    using hwcpipe::device::hwcnt::prfcnt_set;
    using hwcpipe::device::hwcnt::sampler::configuration;
    using hwcpipe::device::hwcnt::sampler::vinstr::convert;
    using enable_map_type = hwcpipe::device::hwcnt::sampler::configuration::enable_map_type;

    SECTION("configuration::enable_map_type") {
        uint32_t expected = 0;
        enable_map_type mask;

        // Note: each flag enables a group of 4 counters.
        std::tie(expected, mask) =
            GENERATE(std::make_pair(uint32_t{0b0}, enable_map_type{0b0}),                                      //
                     std::make_pair(uint32_t{0b1}, enable_map_type{0b0001}),                                   //
                     std::make_pair(uint32_t{0b1}, enable_map_type{0b0010}),                                   //
                     std::make_pair(uint32_t{0b1}, enable_map_type{0b0100}),                                   //
                     std::make_pair(uint32_t{0b1}, enable_map_type{0b1010}),                                   //
                     std::make_pair(uint32_t{0b1010}, enable_map_type{0xF0F0}),                                //
                     std::make_pair(uint32_t{0b11111010}, enable_map_type{0xFFFFF0F0}),                        //
                     std::make_pair(uint32_t{0b10101010101010101010101010101010}, generate_huge_enable_mask()) //
            );

        CHECK(expected == convert(mask));
    }

    SECTION("bad configurations") {
        using configs_type = std::array<configuration, 1>;
        auto configs = GENERATE(configs_type{configuration{block_type::fe, prfcnt_set::secondary}}, //
                                configs_type{configuration{block_type::fe, prfcnt_set::tertiary}});

        std::error_code ec;
        std::tie(ec, std::ignore) = convert(configs.begin(), configs.end());

        CHECK(ec);
    }

    SECTION("configurations") {
        using configs_type = std::array<configuration, 5>;

        configs_type configs = {
            configuration{block_type::fe, prfcnt_set::primary, 0x000F},
            configuration{block_type::tiler, prfcnt_set::primary, 0x00F0},
            configuration{block_type::memory, prfcnt_set::primary, 0x0F00},
            configuration{block_type::core, prfcnt_set::primary, 0x0000},
            configuration{block_type::core, prfcnt_set::primary, 0xF000},
        };

        using hwcpipe::device::ioctl::kbase::hwcnt_reader_setup;

        std::error_code ec;
        hwcnt_reader_setup setup_args{};

        std::tie(ec, setup_args) = convert(configs.begin(), configs.end());

        REQUIRE(!ec);

        CHECK(setup_args.fe_bm == 0b1);
        CHECK(setup_args.tiler_bm == 0b10);
        CHECK(setup_args.mmu_l2_bm == 0b100);
        CHECK(setup_args.shader_bm == 0b1000);
    }
}
