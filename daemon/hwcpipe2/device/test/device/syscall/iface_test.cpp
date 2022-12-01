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

#include <device/syscall/iface.hpp>

SCENARIO("device::detail::syscall::iface", "[unit]") {
    using syscall_iface = hwcpipe::device::syscall::iface;

    GIVEN("an invalid path") {
        WHEN("open is called") {
            int fd = 0;
            std::error_code ec;
            std::tie(ec, fd) = syscall_iface::open("/a/b/c/d/e.txt", O_RDONLY);

            THEN("it fails") {
                CHECK(fd < 0);
                CHECK(ec);
            }
        }
    }
    GIVEN("a valid handle for /dev/zero") {
        int fd = 0;
        std::error_code ec;
        std::tie(ec, fd) = syscall_iface::open("/dev/zero", O_RDONLY);

        REQUIRE(fd > 0);
        CHECK(!ec);

        WHEN("is_char_device is called") {
            bool is_char = false;
            std::tie(ec, is_char) = syscall_iface::is_char_device(fd);
            THEN("the result is true") {
                CHECK(!ec);
                CHECK(is_char);
            }
        }

        WHEN("mmap is called") {
            const size_t length = 4;
            void *addr = nullptr;
            std::tie(ec, addr) = syscall_iface::mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);

            THEN("valid address is returned with no errors") {
                CHECK(addr);
                CHECK(!ec);
            }

            // Call munmap outside of the WHEN clause to avoid addr leaking.
            ec = syscall_iface::munmap(addr, length);
            AND_WHEN("munmap is called") {
                THEN("the memory is unmapped with no errors") { CHECK(!ec); }
            }
        }

        // Call close outside of the WHEN clause to avoid fd leaking.
        ec = syscall_iface::close(fd);
        WHEN("close is called") {
            THEN("handle is closed with no errors") { CHECK(!ec); }
        }
    }
    GIVEN("an invalid handle") {
        const int fd = -42;

        WHEN("is_char_device is called") {
            std::error_code ec;
            bool is_char = false;

            std::tie(ec, is_char) = syscall_iface::is_char_device(fd);
            THEN("it fails") {
                CHECK(ec);
                CHECK(!is_char);
            }
        }
        WHEN("close is called") {
            const std::error_code ec = syscall_iface::close(fd);
            THEN("it fails") { CHECK(ec); }
        }
        WHEN("ioctl is called") {
            const int command = 42;
            std::error_code ec;
            int result = 0;
            std::tie(ec, result) = syscall_iface::ioctl(fd, command);
            THEN("it fails") {
                CHECK(ec);
                CHECK(result == -1);
            }
        }
        WHEN("mmap is called") {
            const size_t length = 4;
            std::error_code ec;
            void *addr = nullptr;

            std::tie(ec, addr) = syscall_iface::mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);

            THEN("it fails") {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
                CHECK(addr == MAP_FAILED);
                CHECK(ec);
            }
        }
    }
}
