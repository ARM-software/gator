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

#include <device/hwcnt/block_metadata_operators.hpp>
#include <device/hwcnt/sampler/vinstr/sample_layout.hpp>

#include <utility>

namespace hwcnt = hwcpipe::device::hwcnt;

namespace hwcpipe {
namespace device {
namespace hwcnt {
namespace sampler {
namespace vinstr {
bool operator==(const sample_layout::entry &lhs, const sample_layout::entry &rhs) {
    return (lhs.type == rhs.type)      //
           && (lhs.index == rhs.index) //
           && (lhs.offset == rhs.offset);
}

bool operator!=(const sample_layout::entry &lhs, const sample_layout::entry &rhs) { return !(lhs == rhs); }

std::ostream &operator<<(std::ostream &os, const sample_layout::entry &value) {
    return os << "sample_layout::entry {\n"                                                  //
              << debug::indent_level::push                                                   //
              << debug::indent << ".type = " << value.type << ",\n"                          //
              << debug::indent << ".index = " << static_cast<uint32_t>(value.index) << ",\n" //
              << debug::indent << ".offset = " << value.offset << ",\n"                      //
              << debug::indent_level::pop                                                    //
              << debug::indent << "}";                                                       //
}

} // namespace vinstr
} // namespace sampler
} // namespace hwcnt
} // namespace device
} // namespace hwcpipe

SCENARIO("device::hwcnt::sampler::vinstr::sample_layout", "[unit]") {
    using hwcpipe::device::hwcnt::sampler::vinstr::sample_layout;
    using sample_layout_type = hwcpipe::device::hwcnt::sampler::vinstr::sample_layout_type;

    GIVEN("one block - non v4 layout") {
        hwcnt::block_type block_type{};
        size_t expected_offset = 0;

        static constexpr uint64_t core_mask = 0b10;
        static constexpr uint64_t num_l2_slices = 1;

        std::tie(block_type, expected_offset) = GENERATE(std::make_pair(hwcnt::block_type::fe, 0x0),       //
                                                         std::make_pair(hwcnt::block_type::tiler, 0x100),  //
                                                         std::make_pair(hwcnt::block_type::memory, 0x200), //
                                                         std::make_pair(hwcnt::block_type::core, 0x400));  //

        hwcnt::block_extents::num_blocks_of_type_type num_blocks_of_type{};
        num_blocks_of_type[static_cast<size_t>(block_type)] = 1;
        hwcnt::block_extents extents{num_blocks_of_type, 64, hwcnt::sample_values_type::uint32};

        WHEN("layout instance is created") {
            sample_layout layout(extents, num_l2_slices, core_mask, sample_layout_type::non_v4);

            THEN("the entry is as expected") {
                REQUIRE(layout.size() == 1);

                sample_layout::entry expected = {block_type, 0, expected_offset};
                const auto &actual = layout[0];

                CHECK(actual == expected);
            }
        }
    }
    GIVEN("all blocks - non v4 layout") {
        hwcnt::block_extents extents{{{1, 1, 2, 2}}, 64, hwcnt::sample_values_type::uint32};

        static constexpr uint64_t core_mask = 0b1100;
        static constexpr uint64_t num_l2_slices = 2;

        WHEN("layout instance is created") {
            const sample_layout layout(extents, num_l2_slices, core_mask, sample_layout_type::non_v4);

            THEN("size == extents.num_blocks") { CHECK(layout.size() == extents.num_blocks()); }
            THEN("the entries are as expected") {
                const std::vector<sample_layout::entry> expected = {
                    {hwcnt::block_type::fe, 0, 0x0},       //
                    {hwcnt::block_type::tiler, 0, 0x100},  //
                    {hwcnt::block_type::memory, 0, 0x200}, //
                    {hwcnt::block_type::memory, 1, 0x300}, //
                    {hwcnt::block_type::core, 0, 0x600},   //
                    {hwcnt::block_type::core, 1, 0x700},   //
                };

                REQUIRE(layout.size() == expected.size());

                for (size_t i = 0; i < expected.size(); ++i)
                    CHECK(layout[i] == expected[i]);
            }
        }
    }

    GIVEN("one block - v4 layout") {
        hwcnt::block_type block_type{};
        size_t expected_offset = 0;

        static constexpr uint64_t core_mask = 0b1000;
        static constexpr uint64_t num_l2_slices = 2;

        std::tie(block_type, expected_offset) = GENERATE(std::make_pair(hwcnt::block_type::core, 0x300),   //
                                                         std::make_pair(hwcnt::block_type::tiler, 0x400),  //
                                                         std::make_pair(hwcnt::block_type::memory, 0x500), //
                                                         std::make_pair(hwcnt::block_type::fe, 0x700));    //

        hwcnt::block_extents::num_blocks_of_type_type num_blocks_of_type{};
        num_blocks_of_type[static_cast<size_t>(block_type)] = 1;
        hwcnt::block_extents extents{num_blocks_of_type, 64, hwcnt::sample_values_type::uint32};

        WHEN("layout instance is created") {
            sample_layout layout(extents, num_l2_slices, core_mask, sample_layout_type::v4);

            THEN("the entry is as expected") {
                REQUIRE(layout.size() == 1);

                sample_layout::entry expected = {block_type, 0, expected_offset};
                const auto &actual = layout[0];

                CHECK(actual == expected);
            }
        }
    }

    GIVEN("all blocks - v4 layout") {
        hwcnt::block_extents extents{{{1, 1, 1, 4}}, 64, hwcnt::sample_values_type::uint32};

        static constexpr uint64_t core_mask = 0b1111;
        static constexpr uint64_t num_l2_slices = 1;

        WHEN("layout instance is created") {
            const sample_layout layout(extents, num_l2_slices, core_mask, sample_layout_type::v4);

            THEN("size == extents.num_blocks") { CHECK(layout.size() == extents.num_blocks()); }
            THEN("the entries are as expected") {
                const std::vector<sample_layout::entry> expected = {
                    {hwcnt::block_type::core, 0, 0x0},     //
                    {hwcnt::block_type::core, 1, 0x100},   //
                    {hwcnt::block_type::core, 2, 0x200},   //
                    {hwcnt::block_type::core, 3, 0x300},   //
                    {hwcnt::block_type::tiler, 0, 0x400},  //
                    {hwcnt::block_type::memory, 0, 0x500}, //
                    {hwcnt::block_type::fe, 0, 0x700},     //
                };

                REQUIRE(layout.size() == expected.size());

                for (size_t i = 0; i < expected.size(); ++i)
                    CHECK(layout[i] == expected[i]);
            }
        }
    }
}
