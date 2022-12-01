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

#include <device/handle.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

SCENARIO("ETE device::handle", "[end-to-end]") {
    namespace device = hwcpipe::device;
    WHEN("handle::create() is called") {
        auto handle = device::handle::create();
        THEN("handle is created successfully") { CHECK(handle != nullptr); }
    }

#if !defined(HWCPIPE_SYSCALL_LIBMALI)
    GIVEN("handle created externally") {
        const int mali_fd = open("/dev/mali0", O_RDONLY);
        REQUIRE(mali_fd >= 0);

        WHEN("handle::from_external_fd(fd) is called") {
            auto handle = device::handle::from_external_fd(mali_fd);
            THEN("handle is created successfully") { CHECK(handle != nullptr); }
        }

        const int result = close(mali_fd);
        CHECK(result == 0);
    }
#endif
}
