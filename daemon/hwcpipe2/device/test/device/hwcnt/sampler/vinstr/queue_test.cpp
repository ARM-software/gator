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

#include <device/hwcnt/sampler/vinstr/queue.hpp>

SCENARIO("device::hwcnt::sampler::vinstr::queue", "[unit]") {
    using hwcpipe::device::hwcnt::sampler::vinstr::queue;

    constexpr size_t max_size = 32;
    using queue_type = queue<uint64_t, max_size>;

    GIVEN("queue instance") {
        queue_type q;

        THEN("empty() is true") { CHECK(q.empty()); }
        THEN("full() is false") { CHECK(!q.full()); }
        THEN("size() == 0") { CHECK(q.size() == 0); }
        THEN("push_count() == 0") { CHECK(q.push_count() == 0); }
        THEN("pop_count() == 0") { CHECK(q.pop_count() == 0); }

        WHEN("push(42) is called") {
            q.push(42);

            THEN("front() == 42") { CHECK(q.front() == 42); }
            THEN("back() == 42") { CHECK(q.back() == 42); }
            THEN("empty() is false") { CHECK(!q.empty()); }
            THEN("full() is false") { CHECK(!q.full()); }
            THEN("size() == 1") { CHECK(q.size() == 1); }
            THEN("push_count() == 1") { CHECK(q.push_count() == 1); }
            THEN("pop_count() == 0") { CHECK(q.pop_count() == 0); }

            AND_WHEN("back() reference is modified") {
                q.back() = 43;
                CHECK(q.back() == 43);
            }

            AND_WHEN("front() reference is modified") {
                q.front() = 43;
                CHECK(q.front() == 43);
            }

            AND_WHEN("pop() is called") {
                q.pop();

                THEN("empty() is true") { CHECK(q.empty()); }
                THEN("full() is false") { CHECK(!q.full()); }
                THEN("size() == 0") { CHECK(q.size() == 0); }
                THEN("push_count() == 1") { CHECK(q.push_count() == 1); }
                THEN("pop_count() == 1") { CHECK(q.pop_count() == 1); }
            }
        }

        WHEN("push() is called max_size times") {
            for (uint64_t i = 0; i < max_size; ++i) {
                q.push(i);
                CHECK(q.back() == i);
            }

            THEN("empty() is false") { CHECK(!q.empty()); }
            THEN("full() is true") { CHECK(q.full()); }
            THEN("size() == max_size") { CHECK(q.size() == max_size); }
            THEN("push_count() == max_size") { CHECK(q.push_count() == max_size); }
            THEN("pop_count() == 0") { CHECK(q.pop_count() == 0); }

            AND_WHEN("pop() is called max_size times") {
                for (uint64_t i = 0; i < max_size; ++i) {
                    CHECK(q.pop() == i);
                }

                THEN("empty() is true") { CHECK(q.empty()); }
                THEN("full() is false") { CHECK(!q.full()); }
                THEN("size() == 0") { CHECK(q.size() == 0); }
                THEN("push_count() == max_size") { CHECK(q.push_count() == max_size); }
                THEN("pop_count() == max_size") { CHECK(q.pop_count() == max_size); }
            }

            AND_THEN("pop() is called (max_size / 2), push() is called (max_size / 2)") {
                constexpr auto max_size_div2 = max_size / 2;
                constexpr auto max_size_1p5 = max_size + max_size_div2;

                for (uint64_t i = 0; i < max_size_div2; ++i) {
                    CHECK(q.pop() == i);
                }

                THEN("push_count() == max_size") { CHECK(q.push_count() == max_size); }
                THEN("pop_count() == (max_size / 2)") { CHECK(q.pop_count() == max_size_div2); }

                for (uint64_t i = max_size; i < max_size_1p5; ++i) {
                    q.push(i);
                    CHECK(q.back() == i);
                }

                THEN("empty() is false") { CHECK(!q.empty()); }
                THEN("full() is true") { CHECK(q.full()); }
                THEN("size() == max_size") { CHECK(q.size() == max_size); }
                THEN("push_count() == (max_size * 1.5)") { CHECK(q.push_count() == max_size_1p5); }
                THEN("pop_count() == (max_size / 2)") { CHECK(q.pop_count() == max_size_div2); }

                AND_THEN("queue is drained") {
                    for (uint64_t i = max_size_div2; i < max_size_1p5; ++i) {
                        CHECK(q.pop() == i);
                    }

                    THEN("empty() is true") { CHECK(q.empty()); }
                    THEN("full() is false") { CHECK(!q.full()); }
                    THEN("size() == 0") { CHECK(q.size() == 0); }
                    THEN("push_count() == (max_size * 1.5)") { CHECK(q.push_count() == max_size_1p5); }
                    THEN("pop_count() == (max_size * 1.5)") { CHECK(q.pop_count() == max_size_1p5); }
                }
            }
        }
    }
}
