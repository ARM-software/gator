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

#include <device/detail/enum_operators.hpp>
#include <device/hwcnt/sampler/kinstr_prfcnt/enum_info.hpp>

namespace kinstr_prfcnt = hwcpipe::device::ioctl::kinstr_prfcnt;

SCENARIO("device::hwcnt::sampler::kinstr_prfcnt::detail::parser_impl", "[unit]") {
    using hwcpipe::device::hwcnt::sampler::kinstr_prfcnt::enum_info;
    using hwcpipe::device::hwcnt::sampler::kinstr_prfcnt::detail::parser_impl;

    using enum_info_vec = std::vector<kinstr_prfcnt::enum_item>;
    using kinstr_prfcnt::block_type;
    using kinstr_prfcnt::prfcnt_set;
    using request_type = kinstr_prfcnt::enum_item::enum_request::request_type;

    GIVEN("enum_info_items array with invalid data") {
        enum_info_vec enum_info_items;
        const char *test_name{nullptr};
        constexpr uint32_t versions_mask{1 << kinstr_prfcnt::api_version};

        std::tie(test_name, enum_info_items) = GENERATE( //
            std::make_pair(                              //
                "One block item is missing",             //
                enum_info_vec{
                    test::enum_item::block(block_type::tiler, prfcnt_set::primary, 1, 64),       //
                    test::enum_item::block(block_type::memory, prfcnt_set::primary, 1, 64),      //
                    test::enum_item::block(block_type::shader_core, prfcnt_set::primary, 1, 64), //
                    test::enum_item::request(request_type::mode, versions_mask),                 //
                    test::enum_item::request(request_type::enable, versions_mask),               //
                    test::enum_item::sample_info(2),                                             //
                    kinstr_prfcnt::enum_item{},                                                  //
                }),                                                                              //
            std::make_pair(                                                                      //
                "There is a block_item duplicate",                                               //
                enum_info_vec{
                    test::enum_item::block(block_type::fe, prfcnt_set::primary, 1, 64),          //
                    test::enum_item::block(block_type::fe, prfcnt_set::primary, 1, 64),          //
                    test::enum_item::block(block_type::tiler, prfcnt_set::primary, 1, 64),       //
                    test::enum_item::block(block_type::memory, prfcnt_set::primary, 1, 64),      //
                    test::enum_item::block(block_type::shader_core, prfcnt_set::primary, 1, 64), //
                    test::enum_item::request(request_type::mode, versions_mask),                 //
                    test::enum_item::request(request_type::enable, versions_mask),               //
                    test::enum_item::sample_info(2),                                             //
                    kinstr_prfcnt::enum_item{},                                                  //
                }),                                                                              //
            std::make_pair(                                                                      //
                "Number of values is inconsistent",                                              //
                enum_info_vec{
                    test::enum_item::block(block_type::fe, prfcnt_set::primary, 1, 128),         //
                    test::enum_item::block(block_type::tiler, prfcnt_set::primary, 1, 64),       //
                    test::enum_item::block(block_type::memory, prfcnt_set::primary, 1, 64),      //
                    test::enum_item::block(block_type::shader_core, prfcnt_set::primary, 1, 64), //
                    test::enum_item::request(request_type::mode, versions_mask),                 //
                    test::enum_item::request(request_type::enable, versions_mask),               //
                    test::enum_item::sample_info(2),                                             //
                    kinstr_prfcnt::enum_item{},                                                  //
                }),                                                                              //
            std::make_pair(                                                                      //
                "prfcnt_set is inconsistent",                                                    //
                enum_info_vec{
                    test::enum_item::block(block_type::fe, prfcnt_set::primary, 1, 64),          //
                    test::enum_item::block(block_type::tiler, prfcnt_set::secondary, 1, 64),     //
                    test::enum_item::block(block_type::memory, prfcnt_set::primary, 1, 64),      //
                    test::enum_item::block(block_type::shader_core, prfcnt_set::primary, 1, 64), //
                    test::enum_item::request(request_type::mode, versions_mask),                 //
                    test::enum_item::request(request_type::enable, versions_mask),               //
                    test::enum_item::sample_info(2),                                             //
                    kinstr_prfcnt::enum_item{},                                                  //
                }),                                                                              //
            std::make_pair(                                                                      //
                "Enable request is not supported",                                               //
                enum_info_vec{
                    test::enum_item::block(block_type::fe, prfcnt_set::primary, 1, 64),          //
                    test::enum_item::block(block_type::tiler, prfcnt_set::primary, 1, 64),       //
                    test::enum_item::block(block_type::memory, prfcnt_set::primary, 1, 64),      //
                    test::enum_item::block(block_type::shader_core, prfcnt_set::primary, 1, 64), //
                    test::enum_item::request(request_type::mode, versions_mask),                 //
                    test::enum_item::sample_info(2),                                             //
                    kinstr_prfcnt::enum_item{},                                                  //
                }),                                                                              //
            std::make_pair(                                                                      //
                "Mode request is not supported",                                                 //
                enum_info_vec{
                    test::enum_item::block(block_type::fe, prfcnt_set::primary, 1, 64),          //
                    test::enum_item::block(block_type::tiler, prfcnt_set::primary, 1, 64),       //
                    test::enum_item::block(block_type::memory, prfcnt_set::primary, 1, 64),      //
                    test::enum_item::block(block_type::shader_core, prfcnt_set::primary, 1, 64), //
                    test::enum_item::request(request_type::enable, versions_mask),               //
                    test::enum_item::sample_info(2),                                             //
                    kinstr_prfcnt::enum_item{},                                                  //
                }),                                                                              //
            std::make_pair(                                                                      //
                "Duplicate request entry",                                                       //
                enum_info_vec{
                    test::enum_item::block(block_type::fe, prfcnt_set::primary, 1, 64),          //
                    test::enum_item::block(block_type::tiler, prfcnt_set::primary, 1, 64),       //
                    test::enum_item::block(block_type::memory, prfcnt_set::primary, 1, 64),      //
                    test::enum_item::block(block_type::shader_core, prfcnt_set::primary, 1, 64), //
                    test::enum_item::request(request_type::mode, versions_mask),                 //
                    test::enum_item::request(request_type::mode, versions_mask),                 //
                    test::enum_item::request(request_type::enable, versions_mask),               //
                    test::enum_item::sample_info(2),                                             //
                    kinstr_prfcnt::enum_item{},                                                  //
                }),                                                                              //
            std::make_pair(                                                                      //
                "Missing sample info",                                                           //
                enum_info_vec{
                    test::enum_item::block(block_type::fe, prfcnt_set::primary, 1, 64),          //
                    test::enum_item::block(block_type::tiler, prfcnt_set::primary, 1, 64),       //
                    test::enum_item::block(block_type::memory, prfcnt_set::primary, 1, 64),      //
                    test::enum_item::block(block_type::shader_core, prfcnt_set::primary, 1, 64), //
                    test::enum_item::request(request_type::mode, versions_mask),                 //
                    test::enum_item::request(request_type::enable, versions_mask),               //
                    kinstr_prfcnt::enum_item{},                                                  //
                }),                                                                              //
            std::make_pair(                                                                      //
                "Duplicate sample_info entry",                                                   //
                enum_info_vec{
                    test::enum_item::block(block_type::fe, prfcnt_set::primary, 1, 64),          //
                    test::enum_item::block(block_type::tiler, prfcnt_set::primary, 1, 64),       //
                    test::enum_item::block(block_type::memory, prfcnt_set::primary, 1, 64),      //
                    test::enum_item::block(block_type::shader_core, prfcnt_set::primary, 1, 64), //
                    test::enum_item::request(request_type::mode, versions_mask),                 //
                    test::enum_item::request(request_type::enable, versions_mask),               //
                    test::enum_item::sample_info(2),                                             //
                    test::enum_item::sample_info(2),                                             //
                    kinstr_prfcnt::enum_item{},                                                  //
                }),                                                                              //
            std::make_pair(                                                                      //
                "Num clocks is too high",                                                        //
                enum_info_vec{
                    test::enum_item::block(block_type::fe, prfcnt_set::primary, 1, 64),          //
                    test::enum_item::block(block_type::tiler, prfcnt_set::primary, 1, 64),       //
                    test::enum_item::block(block_type::memory, prfcnt_set::primary, 1, 64),      //
                    test::enum_item::block(block_type::shader_core, prfcnt_set::primary, 1, 64), //
                    test::enum_item::request(request_type::mode, versions_mask),                 //
                    test::enum_item::request(request_type::enable, versions_mask),               //
                    test::enum_item::sample_info(5),                                             //
                    kinstr_prfcnt::enum_item{},                                                  //
                }),                                                                              //
            std::make_pair(                                                                      //
                "No sentinel item",                                                              //
                enum_info_vec{
                    test::enum_item::block(block_type::fe, prfcnt_set::primary, 1, 64),          //
                    test::enum_item::block(block_type::tiler, prfcnt_set::primary, 1, 64),       //
                    test::enum_item::block(block_type::memory, prfcnt_set::primary, 1, 64),      //
                    test::enum_item::block(block_type::shader_core, prfcnt_set::primary, 1, 64), //
                    test::enum_item::request(request_type::mode, versions_mask),                 //
                    test::enum_item::request(request_type::enable, versions_mask),               //
                    test::enum_item::sample_info(5),                                             //
                })                                                                               //

        );

        WHEN(test_name) {
            std::error_code ec;
            enum_info ei{};
            parser_impl parser{};

            std::tie(ec, ei) = parser.parse(enum_info_items.begin(), enum_info_items.end());

            THEN("error is returned") { CHECK(ec); }
        }
    }

    GIVEN("enum_info_items array with valid data") {
        const uint16_t num_values = GENERATE(64, 128);
        const uint32_t num_clock_domains = GENERATE(0, 1, 2, 4);
        const uint32_t versions_mask = GENERATE(1 << 0, (1 << 1) | (1 << 0));
        const auto set = GENERATE(prfcnt_set::primary, prfcnt_set::secondary);

        using mask_type = std::array<uint64_t, 2>;

        mask_type expected_mask;
        mask_type enum_info_mask;
        mask_type request_mask;

        std::tie(expected_mask, enum_info_mask, request_mask) = GENERATE(                                     //
            std::make_tuple(mask_type{0xFFFF, 0xFFFF}, mask_type{0xFFFF, 0xFFFF}, mask_type{0xFFFF, 0xFFFF}), //
            std::make_tuple(mask_type{0xFF, 0xFF}, mask_type{0xFF, 0xFF}, mask_type{0xFFFF, 0xFFFF}),         //
            std::make_tuple(mask_type{0xFF, 0xFF}, mask_type{0xFFFF, 0xFFFF}, mask_type{0xFF, 0xFF}),         //
            std::make_tuple(mask_type{0, 0}, mask_type{0, 0xFFFF}, mask_type{0xFFFF, 0})                      //
        );

        CAPTURE(num_values);
        CAPTURE(num_clock_domains);

        const auto enum_info_items = enum_info_vec{
            test::enum_item::block(block_type::fe, set, 1, num_values, enum_info_mask),           //
            test::enum_item::block(block_type::tiler, set, 1, num_values, enum_info_mask),        //
            test::enum_item::block(block_type::memory, set, 2, num_values, enum_info_mask),       //
            test::enum_item::block(block_type::shader_core, set, 10, num_values, enum_info_mask), //
            test::enum_item::request(request_type::mode, versions_mask),                          //
            test::enum_item::request(request_type::enable, versions_mask),                        //
            test::enum_item::sample_info(num_clock_domains),                                      //
            kinstr_prfcnt::enum_item{},
        };

        WHEN("enum_info is parsed") {
            std::error_code ec;
            enum_info ei{};
            parser_impl parser{};
            std::tie(ec, ei) = parser.parse(enum_info_items.begin(), enum_info_items.end());

            REQUIRE(!ec);

            THEN("values are as expected") {
                using hwcpipe::device::hwcnt::sampler::kinstr_prfcnt::convert;

                CHECK(ei.set == convert(set));
                CHECK(ei.blocks[0].num_instances == 1);
                CHECK(ei.blocks[1].num_instances == 1);
                CHECK(ei.blocks[2].num_instances == 2);
                CHECK(ei.blocks[3].num_instances == 10);
                CHECK(ei.has_cycles_top == (num_clock_domains >= 1));
                CHECK(ei.has_cycles_sc == (num_clock_domains >= 2));
            }
        }
    }
}
