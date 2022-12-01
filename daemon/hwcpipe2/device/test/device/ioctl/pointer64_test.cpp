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

#include <device/ioctl/pointer64.hpp>

#include <array>

SCENARIO("device::ioctl::pointer64", "[unit]") {
    using hwcpipe::device::ioctl::pointer64;

    GIVEN("null pointers") {
        constexpr uint64_t zero = 0;

        pointer64<int> ptr_default;
        pointer64<int> ptr_nullptr(nullptr);
        pointer64<int> ptr_copy(ptr_nullptr);
        pointer64<int> ptr_uint64(zero);

        std::array<pointer64<int>, 4> all_ptr{ptr_default, ptr_nullptr, ptr_copy, ptr_uint64};

        WHEN("get is called") {
            THEN("it returns nullptr") {
                for (const auto ptr : all_ptr)
                    CHECK(ptr.get() == nullptr);
            }
        }

        WHEN("operator bool() is called") {
            THEN("it returns false") {
                for (const auto &ptr : all_ptr)
                    CHECK(!ptr);
            }
        }

        WHEN("as_uint64 is called") {
            THEN("zero is returned") {
                for (const auto ptr : all_ptr)
                    CHECK(ptr.as_uint64() == 0);
            }
        }

        WHEN("iterated over all pairs") {
            THEN("they all are same") {
                for (const auto lhs : all_ptr) {
                    for (const auto rhs : all_ptr) {
                        CHECK(lhs == rhs);
                        CHECK(!(lhs != rhs));
                        CHECK(!(lhs < rhs));
                        CHECK(!(lhs > rhs));
                        CHECK(lhs <= rhs);
                        CHECK(lhs >= rhs);
                    }
                }
            }
        }
    }

    GIVEN("pair of pointers") {
        std::array<int, 2> ints = {42, 43};

        pointer64<int> lhs = ints.data() + 0;
        pointer64<int> rhs = ints.data() + 1;

        WHEN("operator bool is called") {
            THEN("it returns true") {
                CHECK(lhs);
                CHECK(rhs);
            }
        }

        WHEN("get is called") {
            THEN("the pointer points to the array item") {
                CHECK(lhs.get() == (ints.data() + 0));
                CHECK(rhs.get() == (ints.data() + 1));
            }
        }

        WHEN("operator* is called") {
            THEN("we get values from the array") {
                CHECK(*lhs == 42);
                CHECK(*rhs == 43);
            }
        }

        WHEN("they are compared") {
            THEN("the result is as expected") {
                CHECK(!(lhs == rhs));
                CHECK(lhs != rhs);
                CHECK(lhs < rhs);
                CHECK(!(lhs > rhs));
                CHECK(lhs <= rhs);
                CHECK(!(lhs >= rhs));
            }
        }

        WHEN("lhs is reset to rhs value") {
            lhs.reset(rhs.get());
            THEN("pointers are equal") { CHECK(lhs == rhs); }
        }
    }

    GIVEN("pointer to a simple struct") {
        struct test_struct {
            int var;
        };

        test_struct test{42};
        pointer64<test_struct> ptr(&test);

        WHEN("operator-> is called") {
            THEN("the member can be read") { CHECK(ptr->var == 42); }
        }
    }
}
