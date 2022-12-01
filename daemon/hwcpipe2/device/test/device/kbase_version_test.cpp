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

#include <device/kbase_version.hpp>

SCENARIO("device::kbase_version", "[unit]") {
    using hwcpipe::device::ioctl_iface_type;
    using hwcpipe::device::kbase_version;

    GIVEN("init") {
        WHEN("empty") {
            kbase_version version{};
            REQUIRE(version.major() == 0);
            REQUIRE(version.minor() == 0);
            REQUIRE(version.type() == ioctl_iface_type::csf);
        }
        WHEN("non-empty") {
            kbase_version version{11, 34, ioctl_iface_type::jm_post_r21};
            REQUIRE(version.major() == 11);
            REQUIRE(version.minor() == 34);
            REQUIRE(version.type() == ioctl_iface_type::jm_post_r21);
        }
    }
    GIVEN("compare") {
        WHEN("equal") {
            kbase_version version1{1, 10, ioctl_iface_type::csf};
            kbase_version version2{1, 10, ioctl_iface_type::csf};
            REQUIRE(version1 == version2);
        }
        WHEN("not-equal") {
            kbase_version version1{1, 10, ioctl_iface_type::jm_post_r21};
            kbase_version version2{11, 34, ioctl_iface_type::jm_post_r21};
            REQUIRE(version1 != version2);
        }
        WHEN("less-than") {
            kbase_version version1{1, 10, ioctl_iface_type::csf};
            kbase_version version2{11, 34, ioctl_iface_type::csf};
            REQUIRE(version1 < version2);
        }
        WHEN("less-than-equal-to") {
            kbase_version version1{1, 10, ioctl_iface_type::jm_post_r21};
            kbase_version version2{1, 10, ioctl_iface_type::jm_post_r21};
            REQUIRE(version1 <= version2);
        }
        WHEN("greater-than") {
            kbase_version version1{1, 10, ioctl_iface_type::csf};
            kbase_version version2{11, 34, ioctl_iface_type::csf};
            REQUIRE(version2 > version1);
        }
        WHEN("greater-than-equal-to") {
            kbase_version version1{1, 10, ioctl_iface_type::jm_post_r21};
            kbase_version version2{1, 10, ioctl_iface_type::jm_post_r21};
            REQUIRE(version1 >= version2);
        }
    }
}
