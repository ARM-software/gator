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

SCENARIO("device::hwcnt::blocks_view", "[unit]") {
    const size_t num_iterations = GENERATE(0u, 1u, 2u, 10u);
    GIVEN("Mock reader with " << num_iterations << " blocks and a blocks_view") {
        using hwcpipe::device::hwcnt::blocks_view;
        using hwcpipe::device::hwcnt::sample_handle;
        using hwcpipe::device::hwcnt::sample_metadata;

        mock::reader reader{num_iterations};
        sample_metadata metadata{};
        sample_handle sample_hndl;
        reader.get_sample(metadata, sample_hndl);

        blocks_view view{reader, sample_hndl};

        WHEN("blocks are iterated") {
            size_t counter = 0;
            for (const auto &block : view) {
                CHECK(counter == block.index);
                ++counter;
            }

            THEN("The loop does " << num_iterations << " iterations") { CHECK(num_iterations == counter); }
            THEN("reader::next is called " << num_iterations + 1 << " times") {
                CHECK(num_iterations + 1 == reader.num().next);
            }
        }
    }
}
