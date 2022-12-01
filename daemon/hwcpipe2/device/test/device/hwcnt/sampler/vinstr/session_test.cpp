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

#include <device/hwcnt/sampler/vinstr/session.hpp>

SCENARIO("device::hwcnt::sampler::vinstr::session", "[unit]") {
    using hwcpipe::device::hwcnt::sampler::vinstr::session;

    constexpr uint64_t start_ts = 100000;
    constexpr uint64_t user_data = 12345678;

    GIVEN("session instance") {
        session s{start_ts, user_data};

        CHECK(s.user_data_periodic() == user_data);
        CHECK(!s.can_erase(0));

        WHEN("update_ts() is called") {
            THEN("previous time is returned and the new time is stored") {
                constexpr uint64_t delta = 42;

                CHECK(s.update_ts(start_ts + delta * 1) == (start_ts + delta * 0));
                CHECK(s.update_ts(start_ts + delta * 2) == (start_ts + delta * 1));
                CHECK(s.update_ts(start_ts + delta * 3) == (start_ts + delta * 2));
                CHECK(s.update_ts(start_ts + delta * 4) == (start_ts + delta * 3));
            }
        }

        WHEN("stop() is called") {
            constexpr uint64_t stop_sample_nr = 1000;
            s.stop(stop_sample_nr);

            THEN("can_erase() returns true for matching sample number") { CHECK(s.can_erase(stop_sample_nr)); }

            THEN("can_erase() returns false for not matching sample number") {
                constexpr uint64_t other_sample_nr = 999;
                CHECK(!s.can_erase(other_sample_nr));
            }
        }
    }
}
