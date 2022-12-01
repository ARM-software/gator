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

#include <device/hwcnt/sampler/kinstr_prfcnt/convert.hpp>

TEST_CASE("device::hwcnt::sampler::kinstr_prfcnt::convert", "[unit]") {
    using hwcpipe::device::hwcnt::sampler::configuration;
    using hwcpipe::device::hwcnt::sampler::kinstr_prfcnt::convert;

    SECTION("enable_map_type") {
        using result_type = std::array<uint64_t, 2>;
        configuration::enable_map_type mask{};
        result_type expected{};
        std::tie(mask, expected) = GENERATE(                                            //
            std::make_pair(configuration::enable_map_type(0), result_type{0, 0}),       //
            std::make_pair(configuration::enable_map_type(1), result_type{1, 0}),       //
            std::make_pair(configuration::enable_map_type(1234), result_type{1234, 0}), //
            std::make_pair(configuration::enable_map_type(1234) | (configuration::enable_map_type(5678) << 64),
                           result_type{1234, 5678}) //
        );

        CHECK(expected == convert(mask));
        CHECK(convert(convert(mask)) == mask);
    }

    SECTION("configuration") {
        using namespace hwcpipe::device::hwcnt;
        namespace kinstr_prfcnt = hwcpipe::device::ioctl::kinstr_prfcnt;

        configuration cfg{};
        cfg.type = block_type::memory;
        cfg.set = prfcnt_set::secondary;
        cfg.enable_map = configuration::enable_map_type(1234) | (configuration::enable_map_type(5678) << 64);

        const auto result = convert(cfg);

        CHECK(result.hdr.type == kinstr_prfcnt::request_item::item_type::enable);
        CHECK(result.hdr.item_version == kinstr_prfcnt::api_version);

        CHECK(result.u.req_enable.type == kinstr_prfcnt::block_type::memory);
        CHECK(result.u.req_enable.set == kinstr_prfcnt::prfcnt_set::secondary);
        CHECK(result.u.req_enable.enable_mask[0] == 1234);
        CHECK(result.u.req_enable.enable_mask[1] == 5678);
    }
}
