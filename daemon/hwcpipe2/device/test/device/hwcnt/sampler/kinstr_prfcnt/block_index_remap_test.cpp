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

#include <device/hwcnt/sampler/kinstr_prfcnt/block_index_remap.hpp>

namespace hwcnt = hwcpipe::device::hwcnt;

template <typename block_index_remap_t>
static inline void test_nop(block_index_remap_t &remap) {
    using hwcnt::block_type;

    block_type type = GENERATE(block_type::fe, block_type::tiler, block_type::memory, block_type::core);
    uint8_t index = GENERATE(0, 1, 2, 3);
    uint8_t new_index{};

    std::error_code ec;
    std::tie(ec, new_index) = remap.remap(type, index);

    REQUIRE(!ec);
    CHECK(new_index == index);
}

TEST_CASE("device::hwcnt::sampler::kinstr_prfcnt::block_index_remap", "[unit]") {
    using hwcnt::block_type;
    using hwcnt::sampler::kinstr_prfcnt::block_index_remap;
    using hwcnt::sampler::kinstr_prfcnt::block_index_remap_nop;

    SECTION("block_index_remap") {
        SECTION("no gaps") {
            const block_index_remap remap{0b1111};
            test_nop(remap);
        }
        SECTION("gaps") {
            const block_index_remap remap{0b1010};

            CHECK(remap.remap(block_type::core, 0) ==
                  std::make_pair(std::make_error_code(std::errc::invalid_argument), uint8_t{}));
            CHECK(remap.remap(block_type::core, 1) == std::make_pair(std::error_code{}, uint8_t{0}));
            CHECK(remap.remap(block_type::core, 2) ==
                  std::make_pair(std::make_error_code(std::errc::invalid_argument), uint8_t{}));
            CHECK(remap.remap(block_type::core, 3) == std::make_pair(std::error_code{}, uint8_t{1}));
        }
    }
    SECTION("block_index_remap_nop") {
        const block_index_remap_nop remap{GENERATE(0b1111, 0b1010)};
        test_nop(remap);
    }
}
