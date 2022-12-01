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

#include <device/product_id.hpp>

SCENARIO("device::product_id", "[unit]") {
    using hwcpipe::device::product_id;

    using version_style = product_id::version_style;
    using gpu_family = product_id::gpu_family;
    using gpu_frontend = product_id::gpu_frontend;

    GIVEN("Legacy style GPU id") {
        uint64_t gpu_id = 0;
        product_id::version_style expected_version_style{};

        constexpr uint32_t gpu_id_t60x = 0x6956U;

        auto test_config = GENERATE_REF(std::make_tuple(gpu_id_t60x, version_style::legacy_t60x),
                                        std::make_tuple(0x600U, version_style::legacy_txxx),
                                        std::make_tuple(0x620U, version_style::legacy_txxx));
        std::tie(gpu_id, expected_version_style) = test_config;

        WHEN("product_id is parsed from gpu_id = " << std::hex << gpu_id) {
            product_id pid{gpu_id};
            THEN("its properties are as expected") {
                CHECK(pid.get_version_style() == expected_version_style);
                CHECK(pid.get_gpu_family() == gpu_family::midgard);
                CHECK(pid.get_gpu_frontend() == gpu_frontend::jm);
            }
        }
    }

    GIVEN("Arch/product major versions style GPU id") {
        uint64_t gpu_id = 0;
        product_id::gpu_family expected_gpu_family{};
        product_id::gpu_frontend expected_gpu_frontend{};
        uint32_t expected_arch_major = 0;
        uint32_t expected_product_major = 0;

        auto test_config = GENERATE(std::make_tuple(0x6000U, 0x6U, 0x0U, gpu_family::bifrost, gpu_frontend::jm),
                                    std::make_tuple(0x6001U, 0x6U, 0x1U, gpu_family::bifrost, gpu_frontend::jm),
                                    std::make_tuple(0x6421U, 0x6U, 0x1U, gpu_family::bifrost, gpu_frontend::jm),
                                    std::make_tuple(0x9000U, 0x9U, 0x0U, gpu_family::valhall, gpu_frontend::jm),
                                    std::make_tuple(0x9002U, 0x9U, 0x2U, gpu_family::valhall, gpu_frontend::jm),
                                    std::make_tuple(0xA004U, 0xaU, 0x4U, gpu_family::valhall, gpu_frontend::csf));

        std::tie(gpu_id, expected_arch_major, expected_product_major, expected_gpu_family, expected_gpu_frontend) =
            test_config;
        WHEN("product_id is parsed from gpu_id = " << std::hex << gpu_id) {
            product_id pid{gpu_id};
            THEN("its properties are as expected") {
                CHECK(pid.get_gpu_family() == expected_gpu_family);
                CHECK(pid.get_gpu_frontend() == expected_gpu_frontend);

                REQUIRE(pid.get_version_style() == version_style::arch_product_major);
                CHECK(pid.get_arch_major() == expected_arch_major);
                CHECK(pid.get_product_major() == expected_product_major);
            }
        }
    }

    GIVEN("product_id value") {
        product_id pid{0x6000U};

        WHEN("Used in switch") {
            THEN("The code compiles fine and the switch works") {
                switch (pid) {
                case product_id{0x6000U}:
                    break;
                default:
                    FAIL();
                }
            }
        }
    }
}
