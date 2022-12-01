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

#include <device/ioctl/offset_pointer.hpp>

#include <array>

SCENARIO("device::ioctl::offset_pointer", "[unit]") {
    using hwcpipe::device::ioctl::offset_pointer;

    GIVEN("Null offsets") {
        int some_var = 42;

        offset_pointer<int, uint32_t> ptr_default;
        offset_pointer<int, uint32_t> ptr_zero(0);
        offset_pointer<int, uint32_t> ptr_ptr_base(&some_var, &some_var);

        WHEN("compared") {
            THEN("all same") {
                CHECK(ptr_default == ptr_zero);
                CHECK(ptr_zero == ptr_ptr_base);
            }
        }
    }

    GIVEN("array of ints and offset_pointers for each element") {
        std::array<int, 4> array = {0, 1, 2, 3};
        using test_type = offset_pointer<int, uint32_t>;
        std::array<test_type, 4> array_ptr = {
            test_type{array.data() + 0, array.data()},
            test_type{array.data() + 1, array.data()},
            test_type{array.data() + 2, array.data()},
            test_type{array.data() + 3, array.data()},
        };

        WHEN("get() is called") {
            THEN("the array value can be read")

            {
                CHECK(*array_ptr[0].get(array.data()) == 0);
                CHECK(*array_ptr[1].get(array.data()) == 1);
                CHECK(*array_ptr[2].get(array.data()) == 2);
                CHECK(*array_ptr[3].get(array.data()) == 3);
            }
        }

        WHEN("offset() is called") {
            THEN("the offset value is returned") {
                CHECK(array_ptr[0].offset() == sizeof(int) * 0);
                CHECK(array_ptr[1].offset() == sizeof(int) * 1);
                CHECK(array_ptr[2].offset() == sizeof(int) * 2);
                CHECK(array_ptr[3].offset() == sizeof(int) * 3);
            }
        }

        WHEN("operator== is called") {
            THEN("ptr == ptr = true") { CHECK(array_ptr[0] == array_ptr[0]); }

            THEN("pointers are different") {
                CHECK(!(array_ptr[0] == array_ptr[1]));
                CHECK(!(array_ptr[1] == array_ptr[2]));
                CHECK(!(array_ptr[2] == array_ptr[3]));
            }
        }

        WHEN("operator!= is called") {
            THEN("ptr != ptr = true") { CHECK(!(array_ptr[0] != array_ptr[0])); }

            THEN("pointers are different") {
                CHECK(array_ptr[0] != array_ptr[1]);
                CHECK(array_ptr[1] != array_ptr[2]);
                CHECK(array_ptr[2] != array_ptr[3]);
            }
        }

        WHEN("operator< is called") {
            THEN("ptr < ptr = false") { CHECK(!(array_ptr[0] < array_ptr[0])); }
            THEN("pointers are ordered") {
                CHECK(array_ptr[0] < array_ptr[1]);
                CHECK(array_ptr[1] < array_ptr[2]);
                CHECK(array_ptr[2] < array_ptr[3]);
            }
        }

        WHEN("operator> is called") {
            THEN("ptr > ptr = false") { CHECK(!(array_ptr[0] > array_ptr[0])); }
            THEN("pointers are ordered") {
                CHECK(!(array_ptr[0] > array_ptr[1]));
                CHECK(!(array_ptr[1] > array_ptr[2]));
                CHECK(!(array_ptr[2] > array_ptr[3]));
            }
        }

        WHEN("operator<= is called") {
            THEN("ptr <= ptr = true") { CHECK(array_ptr[0] <= array_ptr[0]); }
            THEN("pointers are ordered") {
                CHECK(array_ptr[0] <= array_ptr[1]);
                CHECK(array_ptr[1] <= array_ptr[2]);
                CHECK(array_ptr[2] <= array_ptr[3]);
            }
        }

        WHEN("operator>= is called") {
            THEN("ptr >= ptr = true") { CHECK(array_ptr[0] >= array_ptr[0]); }
            THEN("pointers are ordered") {
                CHECK(!(array_ptr[0] >= array_ptr[1]));
                CHECK(!(array_ptr[1] >= array_ptr[2]));
                CHECK(!(array_ptr[2] >= array_ptr[3]));
            }
        }
    }
}
