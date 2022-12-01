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

#include <device/hwcnt/sampler/poll.hpp>
#include <device/mock/syscall/iface.hpp>

#include <system_error>

TEST_CASE("device::hwcnt::sampler::mapped_memory", "[unit]") {
    mock::syscall::iface syscall;

    constexpr int test_fd = 42;

    std::error_code expected_ec;
    std::error_code poll_ec;
    int poll_retval{};
    int expected_timeout{};

    syscall.poll_fn = [&](struct pollfd *fds, nfds_t nfds, int timeout) {
        CHECK(timeout == expected_timeout);

        REQUIRE(nfds == 1);
        REQUIRE(fds != nullptr);

        CHECK(fds[0].fd == test_fd);
        CHECK(fds[0].events == POLLIN);

        return std::make_pair(poll_ec, poll_retval);
    };

    SECTION("wait_for_sample") {
        using hwcpipe::device::hwcnt::sampler::wait_for_sample;

        expected_timeout = -1;

        std::tie(expected_ec, poll_ec, poll_retval) =
            GENERATE(std::make_tuple(std::error_code{}, std::error_code{}, 1),
                     std::make_tuple(std::make_error_code(std::errc::invalid_argument),
                                     std::make_error_code(std::errc::invalid_argument), 0),
                     std::make_tuple(std::make_error_code(std::errc::timed_out), std::error_code{}, 0));

        CHECK(wait_for_sample(test_fd, syscall) == expected_ec);
    }
    SECTION("check_ready_read") {
        using hwcpipe::device::hwcnt::sampler::check_ready_read;

        bool expected_is_ready{};

        expected_timeout = 0;

        std::tie(expected_ec, poll_ec, poll_retval, expected_is_ready) =
            GENERATE(std::make_tuple(std::error_code{}, std::error_code{}, 1, true),
                     std::make_tuple(std::error_code{}, std::error_code{}, 0, false),
                     std::make_tuple(std::make_error_code(std::errc::invalid_argument),
                                     std::make_error_code(std::errc::invalid_argument), 0, false));

        std::error_code actual_ec;
        bool actual_is_ready{};

        std::tie(actual_ec, actual_is_ready) = check_ready_read(test_fd, syscall);
        CHECK(actual_ec == expected_ec);
        CHECK(actual_is_ready == expected_is_ready);
    }
}
