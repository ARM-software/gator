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

#include <cstdint>

SCENARIO("device::hwcnt::block_iterator", "[unit]") {
    using hwcpipe::device::hwcnt::block_iterator;
    using hwcpipe::device::hwcnt::sample_handle;
    using hwcpipe::device::hwcnt::sample_metadata;

    GIVEN("Mock reader, sample with 10 blocks and begin/end iterators") {
        mock::reader reader{10};
        sample_metadata metadata{};
        sample_handle sample_hndl;

        reader.get_sample(metadata, sample_hndl);

        block_iterator end{};
        block_iterator begin{reader, sample_hndl};

        CHECK(reader.num().next == 1);
        CHECK(begin == begin);
        CHECK(end == end);
        CHECK(begin != end);

        WHEN("begin or end is copied or assigned") {
            const block_iterator end_copy = end;
            const block_iterator begin_copy = begin;

            block_iterator begin_assigned = end;
            begin_assigned = begin;

            THEN("the value remains the same") {
                CHECK(end == end_copy);
                CHECK(begin == begin_copy);
                CHECK(begin == begin_assigned);
            }
        }

        WHEN("begin is copied to it") {
            block_iterator it = begin;
            THEN("it can be iterated 10 times before reaching end") {
                size_t counter = 0;
                for (counter = 0; it != end && counter != 10; ++it, ++counter) {
                    CHECK(it->index == counter);
                    CHECK((*it).index == counter);
                }

                CHECK(counter == 10);
                CHECK(it == end);
            }
        }
    }
}
