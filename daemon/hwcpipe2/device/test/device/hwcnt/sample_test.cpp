/*
 * Copyright (c) 2021 ARM Limited.
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

#include "reader_mock.hpp"

#include <catch2/catch.hpp>

#include <device/hwcnt/sample_operators.hpp>

#include <cstring>

SCENARIO("device::hwcnt::sample", "[unit]") {
    using hwcpipe::device::hwcnt::sample;

    GIVEN("A mock reader and a error code") {
        mock::reader reader;
        std::error_code ec;

        WHEN("sample is constructed") {
            {
                sample s{reader, ec};

                THEN("reader::get_sample is called") { CHECK(reader.num().get_sample == 1); }
                THEN("ec has no error") { CHECK(!ec); }
                THEN("sample is true") { CHECK(s); }
            }
            AND_THEN("sample is destructed") {
                THEN("reader::put_sample is called") { CHECK(reader.num().put_sample == 1); }
                THEN("ec still has no error") { CHECK(!ec); }
            }
        }
        WHEN("sample is constructed with a error injected") {
            {
                reader.inject_error();
                sample s{reader, ec};

                THEN("reader::get_sample is called") { CHECK(reader.num().get_sample == 1); }
                THEN("ec has a error") { CHECK(ec); }
                THEN("sample is false") { CHECK(!s); }
            }
            AND_THEN("sample is destructed") {
                THEN("reader::put_sample is not called") { CHECK(reader.num().put_sample == 0); }
                THEN("ec has a error") { CHECK(ec); }
            }
        }
        WHEN("sample is constructed") {
            {
                sample s{reader, ec};
                reader.inject_error();
            }
            AND_THEN("destructed with a error injected") {
                THEN("reader::put_sample is called") { CHECK(reader.num().put_sample == 1); }
                THEN("ec has a error") { CHECK(ec); }
            }
        }
        WHEN("sample is constructed with mock metadata") {
            using hwcpipe::device::hwcnt::sample_metadata;
            const sample_metadata expected_metadata = {
                42, // user_data
                {}, // flags
                43, // sample_nr
                44, // timestamp_ns_begin
                45, // timestamp_ns_end
                46, // gpu_cycle
                47, // sc_cycle
            };
            reader.set_sample_metadata(expected_metadata);
            sample s{reader, ec};

            THEN("sample::get_metadata returns the mock metadata") {
                const sample_metadata actual_metadata = s.get_metadata();

                CHECK(expected_metadata == actual_metadata);
            }
        }
    }
}
