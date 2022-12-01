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

#include <device/hwcnt/sampler/vinstr/timestamp.hpp>

#include <chrono>
#include <thread>

SCENARIO("device::hwcnt::sampler::vinstr::timestamp_iface", "[unit]") {
    using hwcpipe::device::hwcnt::sampler::vinstr::timestamp_iface;

    GIVEN("timestamp_iface") {
        WHEN("two samples requested") {
            using namespace std::chrono_literals;

            const auto sample0 = timestamp_iface::clock_gettime();
            std::this_thread::sleep_for(1ms);
            const auto sample1 = timestamp_iface::clock_gettime();

            THEN("samples are non zero") {
                CHECK(sample0 != 0);
                CHECK(sample1 != 0);
            }

            THEN("the timestamp grows") { CHECK(sample1 > sample0); }
        }
    }
}
