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

#include "mock/syscall/iface.hpp"

#include <device/handle_impl.hpp>

#include <fcntl.h>
#include <unistd.h>

SCENARIO("device::handle", "[unit]") {
    GIVEN("handle created with `create(const char*)`") {
        WHEN("regular file") {
            auto hndl = hwcpipe::device::handle::create("/proc/self/exe");
            THEN("handle is empty") { REQUIRE(!hndl); }
        }
        AND_WHEN("real character device file") {
            auto hndl = hwcpipe::device::handle::create("/dev/null");
            THEN("handle is valid") { REQUIRE(hndl); }
        }
    }

    GIVEN("handle created with `from_external_fd`") {
        WHEN("descriptor is valid") {
            AND_WHEN("real character device file") {
                const int fd = open("/dev/null", O_RDONLY);
                REQUIRE(fd >= 0);
                auto hndl = hwcpipe::device::handle::from_external_fd(fd);
                close(fd);
                THEN("handle is valid") { REQUIRE(hndl); }
            }
        }
    }
}

SCENARIO("device::handle_impl", "[unit]") {
    using handle_type = hwcpipe::device::handle_impl<mock::syscall::iface>;
    GIVEN("external handle") {
        WHEN("`handle_impl::~handle_impl` is called") {
            THEN("close is not called") {
                /* If `hndl.close` is called, the test will fail. */
                handle_type hndl(42, handle_type::mode::external);
            }
        }
    }

    GIVEN("internal handle") {
        mock::syscall::iface iface;
        uint32_t close_count = 0;
        iface.close_fn = [&close_count](int fd) {
            CHECK(fd == 42);
            ++close_count;
            return std::error_code{};
        };

        { handle_type hndl(42, handle_type::mode::internal, iface); }

        WHEN("`handle_impl::~handle_impl` is called") {
            THEN("close is called once") { CHECK(close_count == 1); }
        }
    }

    GIVEN("device file with certain attributes") {
        const char *device_path = "/dev/my_device";

        mock::syscall::iface iface;

        auto test_params = GENERATE(std::make_tuple(-1, false, -1), //
                                    std::make_tuple(42, false, -1), //
                                    std::make_tuple(42, true, 42));

        const int open_return = std::get<0>(test_params);
        const bool is_char_device_return = std::get<1>(test_params);
        const int expected_fd = std::get<2>(test_params);

        CAPTURE(open_return, is_char_device_return, expected_fd);

        uint32_t open_count = 0;
        uint32_t close_count = 0;
        uint32_t is_char_device_count = 0;

        iface.open_fn = [&](const char *path, int mode) {
            ++open_count;
            CHECK(path == device_path);
            CHECK(mode == O_RDONLY);

            if (open_return == -1)
                return std::make_pair(std::make_error_code(std::errc::no_such_file_or_directory), -1);

            return std::make_pair(std::error_code{}, open_return);
        };

        iface.is_char_device_fn = [&](int fd) {
            ++is_char_device_count;
            CHECK(fd == open_return);
            return std::make_pair(std::error_code{}, is_char_device_return);
        };

        iface.close_fn = [&](int fd) {
            ++close_count;
            CHECK(fd == open_return);
            return std::error_code{};
        };

        WHEN("handle_impl::open is called") {
            const int fd = handle_type::open(device_path, iface);

            THEN("the system calls are called certain number of times") {
                CHECK(open_count == 1);

                const uint32_t expected_is_char_device_count = (open_return == -1) ? 0 : 1;
                CHECK(is_char_device_count == expected_is_char_device_count);

                const uint32_t expected_close_count = (expected_fd == -1 && open_return != -1) ? 1 : 0;
                CHECK(close_count == expected_close_count);

                CHECK(fd == expected_fd);
            }
        }
    }
}
