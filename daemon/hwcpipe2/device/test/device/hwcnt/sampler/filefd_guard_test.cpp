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

#include <device/hwcnt/sampler/filefd_guard.hpp>
#include <device/mock/syscall/iface.hpp>

SCENARIO("device::hwcnt::sampler::filefd_guard", "[unit]") {
    using hwcpipe::device::hwcnt::sampler::filefd_guard;

    using filefd_guard_type = filefd_guard<mock::syscall::iface>;

    GIVEN("close mock") {
        constexpr int test_fd = 42;
        bool close_called = false;

        mock::syscall::iface iface;
        iface.close_fn = [&](int fd) {
            CHECK(!close_called);
            CHECK(fd == test_fd);
            close_called = true;

            return std::error_code{};
        };

        WHEN("filefd_guard()") {
            { filefd_guard_type guard(iface); }
            THEN("close is not called") { CHECK(!close_called); }
        }
        WHEN("filefd_guard(-1)") {
            { filefd_guard_type guard(-1, iface); }
            THEN("close is not called") { CHECK(!close_called); }
        }
        WHEN("filefd_guard(test_fd)") {
            {
                filefd_guard_type guard(test_fd, iface);
                AND_THEN("get() is called") { CHECK(guard.get() == test_fd); }
            }
            THEN("close called") { CHECK(close_called); }
        }
        WHEN("filefd_guard() and then reset(test_fd)") {
            {
                filefd_guard_type guard(iface);
                guard.reset(test_fd);
            }
            THEN("close called") { CHECK(close_called); }
        }
        WHEN("filefd_guard(test_fd) and then move-construct") {
            {
                filefd_guard_type guard1(test_fd, iface);
                filefd_guard_type guard2(std::move(guard1));
            }
            THEN("close called") { CHECK(close_called); }
        }
        WHEN("filefd_guard(test_fd) and then move-assign") {
            {
                filefd_guard_type guard1(test_fd, iface);
                filefd_guard_type guard2(iface);
                guard2 = std::move(guard1);
            }
            THEN("close called") { CHECK(close_called); }
        }
        WHEN("filefd_guard(test_fd) and then release()") {
            {
                filefd_guard_type guard(test_fd, iface);
                CHECK(guard.release() == test_fd);
            }
            THEN("close is not called") { CHECK(!close_called); }
        }
    }
}
