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

#include "device/hwcnt/sampler/kinstr_prfcnt/block_index_remap.hpp"
#include "union_init.hpp"

#include <catch2/catch.hpp>

#include <device/hwcnt/block_metadata_operators.hpp>
#include <device/hwcnt/sample_operators.hpp>
#include <device/hwcnt/sampler/kinstr_prfcnt/metadata_parser.hpp>

#include <tuple>
#include <vector>

namespace hwcnt = hwcpipe::device::hwcnt;

namespace test {
static constexpr uint64_t start = 1111;
static constexpr uint64_t stop = 2222;
static constexpr uint64_t seq = 3333;
static constexpr uint64_t user_data = 4444;
static constexpr uint64_t cycle_top = 5555;
static constexpr uint64_t cycle_sc = 6666;
static kinstr_prfcnt::metadata_item::sample_metadata::sample_flag flags{};

static hwcnt::sample_metadata sample_metadata = {
    user_data, // user_data;
    {},        // flags;
    seq,       // sample_nr;
    start,     // timestamp_ns_begin;
    stop,      // timestamp_ns_end;
    cycle_top, // gpu_cycle;
    cycle_sc,  // sc_cycle;
};
} // namespace test

TEST_CASE("device::hwcnt::sampler::kinstr_prfcnt::metadata_parser", "[unit]") {
    namespace kinstr_prfcnt = hwcpipe::device::ioctl::kinstr_prfcnt;
    using hwcpipe::device::hwcnt::sampler::kinstr_prfcnt::block_index_remap;
    using hwcpipe::device::hwcnt::sampler::kinstr_prfcnt::metadata_parser;
    using metadata_vec_type = std::vector<kinstr_prfcnt::metadata_item>;
    using kinstr_prfcnt::block_type;
    using kinstr_prfcnt::prfcnt_set;

    namespace hwcnt = hwcpipe::device::hwcnt;
    hwcnt::sample_metadata sample_metadata{};
    hwcnt::block_extents block_extents{{{1, 1, 1, 2}}, 64, hwcnt::sample_values_type::uint64};

    metadata_parser p{sample_metadata, block_extents};

    uint64_t core_mask{};
    uint8_t sc0{};
    uint8_t sc1{};
    uint8_t sc_bad{};

    std::tie(core_mask, sc0, sc1, sc_bad) = GENERATE(                  //
        std::make_tuple(0b11ULL, uint8_t{0}, uint8_t{1}, uint8_t{2}),  //
        std::make_tuple(0b101ULL, uint8_t{0}, uint8_t{2}, uint8_t{1}), //
        std::make_tuple(0b1010ULL, uint8_t{1}, uint8_t{3}, uint8_t{0}) //
    );

    const block_index_remap remap{core_mask};

    SECTION("bad data") {
        metadata_vec_type metadata_vec;
        const char *test_name{nullptr};

        const block_type extra_block_type =
            GENERATE(block_type::fe, block_type::tiler, block_type::memory, block_type::shader_core);

        std::tie(test_name, metadata_vec) = GENERATE_COPY( //
            std::make_tuple("missing sample",              //
                            metadata_vec_type{
                                test::metadata_item::clock(2, test::cycle_top, test::cycle_sc),                   //
                                test::metadata_item::block(block_type::fe, 0, prfcnt_set::primary, 0),            //
                                test::metadata_item::block(block_type::tiler, 0, prfcnt_set::primary, 0),         //
                                test::metadata_item::block(block_type::memory, 0, prfcnt_set::primary, 0),        //
                                test::metadata_item::block(block_type::shader_core, sc0, prfcnt_set::primary, 0), //
                                test::metadata_item::block(block_type::shader_core, sc1, prfcnt_set::primary, 0), //
                                kinstr_prfcnt::metadata_item{},                                                   //
                            }),                                                                                   //
            std::make_tuple(
                "missing clock", //
                metadata_vec_type{
                    test::metadata_item::sample(test::start, test::stop, test::seq, test::user_data, test::flags), //
                    test::metadata_item::block(block_type::fe, 0, prfcnt_set::primary, 0),                         //
                    test::metadata_item::block(block_type::tiler, 0, prfcnt_set::primary, 0),                      //
                    test::metadata_item::block(block_type::memory, 0, prfcnt_set::primary, 0),                     //
                    test::metadata_item::block(block_type::shader_core, sc0, prfcnt_set::primary, 0),              //
                    test::metadata_item::block(block_type::shader_core, sc1, prfcnt_set::primary, 0),              //
                    kinstr_prfcnt::metadata_item{},                                                                //
                }),                                                                                                //
            std::make_tuple(
                "double sample", //
                metadata_vec_type{
                    test::metadata_item::sample(test::start, test::stop, test::seq, test::user_data, test::flags), //
                    test::metadata_item::sample(test::start, test::stop, test::seq, test::user_data, test::flags), //
                    test::metadata_item::clock(2, test::cycle_top, test::cycle_sc),                                //
                    test::metadata_item::block(block_type::fe, 0, prfcnt_set::primary, 0),                         //
                    test::metadata_item::block(block_type::tiler, 0, prfcnt_set::primary, 0),                      //
                    test::metadata_item::block(block_type::memory, 0, prfcnt_set::primary, 0),                     //
                    test::metadata_item::block(block_type::shader_core, sc0, prfcnt_set::primary, 0),              //
                    test::metadata_item::block(block_type::shader_core, sc1, prfcnt_set::primary, 0),              //
                    kinstr_prfcnt::metadata_item{},                                                                //
                }),                                                                                                //
            std::make_tuple(
                "double clock", //
                metadata_vec_type{
                    test::metadata_item::sample(test::start, test::stop, test::seq, test::user_data, test::flags), //
                    test::metadata_item::clock(2, test::cycle_top, test::cycle_sc),                                //
                    test::metadata_item::clock(2, test::cycle_top, test::cycle_sc),                                //
                    test::metadata_item::block(block_type::fe, 0, prfcnt_set::primary, 0),                         //
                    test::metadata_item::block(block_type::tiler, 0, prfcnt_set::primary, 0),                      //
                    test::metadata_item::block(block_type::memory, 0, prfcnt_set::primary, 0),                     //
                    test::metadata_item::block(block_type::shader_core, sc0, prfcnt_set::primary, 0),              //
                    test::metadata_item::block(block_type::shader_core, sc1, prfcnt_set::primary, 0),              //
                    kinstr_prfcnt::metadata_item{},                                                                //
                }),                                                                                                //
            std::make_tuple(
                "duplicate block index", //
                metadata_vec_type{
                    test::metadata_item::sample(test::start, test::stop, test::seq, test::user_data, test::flags), //
                    test::metadata_item::clock(2, test::cycle_top, test::cycle_sc),                                //
                    test::metadata_item::block(block_type::fe, 0, prfcnt_set::primary, 0),                         //
                    test::metadata_item::block(block_type::tiler, 0, prfcnt_set::primary, 0),                      //
                    test::metadata_item::block(block_type::memory, 0, prfcnt_set::primary, 0),                     //
                    test::metadata_item::block(block_type::shader_core, sc0, prfcnt_set::primary, 0),              //
                    test::metadata_item::block(block_type::shader_core, sc0, prfcnt_set::primary, 0),              //
                    kinstr_prfcnt::metadata_item{},                                                                //
                }),
            std::make_tuple(
                "remap fail", //
                metadata_vec_type{
                    test::metadata_item::sample(test::start, test::stop, test::seq, test::user_data, test::flags), //
                    test::metadata_item::clock(2, test::cycle_top, test::cycle_sc),                                //
                    test::metadata_item::block(block_type::fe, 0, prfcnt_set::primary, 0),                         //
                    test::metadata_item::block(block_type::tiler, 0, prfcnt_set::primary, 0),                      //
                    test::metadata_item::block(block_type::memory, 0, prfcnt_set::primary, 0),                     //
                    test::metadata_item::block(block_type::shader_core, sc0, prfcnt_set::primary, 0),              //
                    test::metadata_item::block(block_type::shader_core, sc_bad, prfcnt_set::primary, 0),           //
                    kinstr_prfcnt::metadata_item{},                                                                //
                }),
            std::make_tuple(
                "extra block", //
                metadata_vec_type{
                    test::metadata_item::sample(test::start, test::stop, test::seq, test::user_data, test::flags), //
                    test::metadata_item::clock(2, test::cycle_top, test::cycle_sc),                                //
                    test::metadata_item::block(block_type::fe, 0, prfcnt_set::primary, 0),                         //
                    test::metadata_item::block(block_type::tiler, 0, prfcnt_set::primary, 0),                      //
                    test::metadata_item::block(block_type::memory, 0, prfcnt_set::primary, 0),                     //
                    test::metadata_item::block(block_type::shader_core, sc0, prfcnt_set::primary, 0),              //
                    test::metadata_item::block(block_type::shader_core, sc1, prfcnt_set::primary, 0),              //
                    test::metadata_item::block(extra_block_type, 0, prfcnt_set::primary, 0),                       //
                    kinstr_prfcnt::metadata_item{},                                                                //
                })                                                                                                 //
        );

        SECTION(test_name) {
            auto ec = p.parse_sample(metadata_vec.begin(), remap);

            CHECK(ec);
        }
    }

    SECTION("good data") {
        metadata_vec_type metadata_vec{
            test::metadata_item::sample(test::start, test::stop, test::seq, test::user_data, test::flags), //
            test::metadata_item::clock(2, test::cycle_top, test::cycle_sc),                                //
            test::metadata_item::block(block_type::fe, 0, prfcnt_set::primary, 1),                         //
            test::metadata_item::block(block_type::tiler, 0, prfcnt_set::primary, 2),                      //
            test::metadata_item::block(block_type::memory, 0, prfcnt_set::primary, 3),                     //
            test::metadata_item::block(block_type::shader_core, sc0, prfcnt_set::primary, 4),              //
            test::metadata_item::block(block_type::shader_core, sc1, prfcnt_set::primary, 5),              //
            kinstr_prfcnt::metadata_item{},                                                                //
        };

        auto ec = p.parse_sample(metadata_vec.begin(), remap);

        REQUIRE(!ec);
        CHECK(sample_metadata == test::sample_metadata);

        SECTION("parse_block") {
            std::array<uint8_t, 6> mapping_data{};

            const std::vector<hwcnt::block_metadata> expected_blocks{
                hwcnt::block_metadata{hwcnt::block_type::fe, 0, hwcnt::prfcnt_set::primary, {}, &mapping_data[1]},
                hwcnt::block_metadata{hwcnt::block_type::tiler, 0, hwcnt::prfcnt_set::primary, {}, &mapping_data[2]},
                hwcnt::block_metadata{hwcnt::block_type::memory, 0, hwcnt::prfcnt_set::primary, {}, &mapping_data[3]},
                hwcnt::block_metadata{hwcnt::block_type::core, 0, hwcnt::prfcnt_set::primary, {}, &mapping_data[4]},
                hwcnt::block_metadata{hwcnt::block_type::core, 1, hwcnt::prfcnt_set::primary, {}, &mapping_data[5]},
            };

            std::vector<hwcnt::block_metadata> actual_blocks;

            auto it = metadata_vec.begin();
            bool has_more_blocks{false};

            for (;;) {
                hwcnt::block_metadata block_metadata{};
                std::tie(has_more_blocks, block_metadata) =
                    metadata_parser::parse_block(it, mapping_data.data(), remap);

                if (!has_more_blocks)
                    break;

                actual_blocks.push_back(block_metadata);

                REQUIRE(actual_blocks.size() <= expected_blocks.size());
            }

            CHECK(actual_blocks == expected_blocks);
        }
    }
}
