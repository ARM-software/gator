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

#include <device/ioctl/strided_array_view.hpp>

#include <array>
#include <iterator>

namespace {
struct test_struct {
    uint32_t uint32_field;
    float float_field;
};

std::array<test_struct, 4> array_data = {
    test_struct{42, 42.0},
    test_struct{43, 43.0},
    test_struct{44, 44.0},
    test_struct{45, 45.0},
};

} // namespace

SCENARIO("device::ioctl::strided_array_iterator", "[unit]") {
    using hwcpipe::device::ioctl::strided_array_iterator;

    GIVEN("array of structs and iterators for its second member") {
        using test_iterator = strided_array_iterator<float>;

        WHEN("used with iterator_traits") {
            using traits_type = std::iterator_traits<test_iterator>;

            THEN("it is a random access itereator") {
                STATIC_REQUIRE(std::is_same<traits_type::value_type, float>::value);
                STATIC_REQUIRE(std::is_same<traits_type::pointer, float *>::value);
                STATIC_REQUIRE(std::is_same<traits_type::reference, float &>::value);
                STATIC_REQUIRE(std::is_same<traits_type::difference_type, ptrdiff_t>::value);
                STATIC_REQUIRE(std::is_same<traits_type::iterator_category, std::random_access_iterator_tag>::value);
            }
        }

        std::array<test_iterator, 4> array_it = GENERATE_REF(
            /* Positive stride. */
            std::array<test_iterator, 4>{
                test_iterator{&array_data[0].float_field, sizeof(test_struct)},
                test_iterator{&array_data[1].float_field, sizeof(test_struct)},
                test_iterator{&array_data[2].float_field, sizeof(test_struct)},
                test_iterator{&array_data[3].float_field, sizeof(test_struct)},
            },
            /* Negative stride. */
            std::array<test_iterator, 4>{
                test_iterator{&array_data[3].float_field, -static_cast<ptrdiff_t>(sizeof(test_struct))},
                test_iterator{&array_data[2].float_field, -static_cast<ptrdiff_t>(sizeof(test_struct))},
                test_iterator{&array_data[1].float_field, -static_cast<ptrdiff_t>(sizeof(test_struct))},
                test_iterator{&array_data[0].float_field, -static_cast<ptrdiff_t>(sizeof(test_struct))},
            });

        WHEN("operator+ is used") {
            THEN("iterator moves forward") {
                CHECK(array_it[0] + 0 == array_it[0]);
                CHECK(array_it[0] + 1 == array_it[1]);
                CHECK(array_it[0] + 2 == array_it[2]);
                CHECK(array_it[0] + 3 == array_it[3]);
            }
        }

        WHEN("operator- is used") {
            THEN("iterator moves backward") {
                CHECK(array_it[3] - 0 == array_it[3]);
                CHECK(array_it[3] - 1 == array_it[2]);
                CHECK(array_it[3] - 2 == array_it[1]);
                CHECK(array_it[3] - 3 == array_it[0]);
            }
        }

        WHEN("iterator difference is used") {
            THEN("distance is returned") {
                CHECK((array_it[3] - array_it[0]) == 3);
                CHECK((array_it[1] - array_it[0]) == 1);
            }
        }

        WHEN("operator++ is used") {
            THEN("iterator moves forward by one") {
                auto it = array_it[0];
                CHECK((++it) == array_it[1]);
                CHECK((++it) == array_it[2]);
                CHECK((++it) == array_it[3]);
                CHECK(it == array_it[3]);
            }
        }

        WHEN("operator++(int) is used") {
            THEN("iterator moves backward by one") {
                auto it = array_it[0];
                CHECK((it++) == array_it[0]);
                CHECK((it++) == array_it[1]);
                CHECK((it++) == array_it[2]);
                CHECK(it == array_it[3]);
            }
        }

        WHEN("operator-- is used") {
            THEN("iterator moves backward by one") {
                auto it = array_it[3];
                CHECK((--it) == array_it[2]);
                CHECK((--it) == array_it[1]);
                CHECK((--it) == array_it[0]);
                CHECK(it == array_it[0]);
            }
        }

        WHEN("operator--(int) is used") {
            THEN("iterator moves forward by one") {
                auto it = array_it[3];
                CHECK((it--) == array_it[3]);
                CHECK((it--) == array_it[2]);
                CHECK((it--) == array_it[1]);
                CHECK(it == array_it[0]);
            }
        }

        WHEN("operator+= is used") {
            THEN("iterator moves forward by one") {
                auto it = array_it[0];
                CHECK((it += 1) == array_it[1]);
                CHECK((it += 1) == array_it[2]);
                CHECK((it += 1) == array_it[3]);
                CHECK(it == array_it[3]);
            }
        }

        WHEN("operator-= is used") {
            THEN("iterator moves backward by one") {
                auto it = array_it[3];
                CHECK((it -= 1) == array_it[2]);
                CHECK((it -= 1) == array_it[1]);
                CHECK((it -= 1) == array_it[0]);
                CHECK(it == array_it[0]);
            }
        }

        WHEN("operator< is used") {
            THEN("it < it = false") { CHECK(!(array_it[0] < array_it[0])); }

            THEN("iterators are ordered") {
                CHECK(array_it[0] < array_it[1]);
                CHECK(!(array_it[1] < array_it[0]));

                CHECK(array_it[1] < array_it[2]);
                CHECK(!(array_it[2] < array_it[1]));

                CHECK(array_it[2] < array_it[3]);
                CHECK(!(array_it[3] < array_it[2]));
            }
        }

        WHEN("operator> is used") {
            THEN("it > it = false") { CHECK(!(array_it[0] > array_it[0])); }

            THEN("iterators are ordered") {
                CHECK(!(array_it[0] > array_it[1]));
                CHECK(array_it[1] > array_it[0]);

                CHECK(!(array_it[1] > array_it[2]));
                CHECK(array_it[2] > array_it[1]);

                CHECK(!(array_it[2] > array_it[3]));
                CHECK(array_it[3] > array_it[2]);
            }
        }

        WHEN("operator<= is used") {
            THEN("it <= it = true") { CHECK(array_it[0] <= array_it[0]); }

            THEN("iterators are ordered") {
                CHECK(array_it[0] <= array_it[1]);
                CHECK(!(array_it[1] <= array_it[0]));

                CHECK(array_it[1] <= array_it[2]);
                CHECK(!(array_it[2] <= array_it[1]));

                CHECK(array_it[2] <= array_it[3]);
                CHECK(!(array_it[3] <= array_it[2]));
            }
        }

        WHEN("operator>= is used") {
            THEN("it >= it = true") { CHECK(array_it[0] >= array_it[0]); }

            THEN("iterators are ordered") {
                CHECK(!(array_it[0] >= array_it[1]));
                CHECK(array_it[1] >= array_it[0]);

                CHECK(!(array_it[1] >= array_it[2]));
                CHECK(array_it[2] >= array_it[1]);

                CHECK(!(array_it[2] >= array_it[3]));
                CHECK(array_it[3] >= array_it[2]);
            }
        }
    }
}

SCENARIO("device::ioctl::strided_array_view", "[unit]") {
    GIVEN("array of structs and iterators for its second member") {
        WHEN("strided_array_view is constructed") {
            using hwcpipe::device::ioctl::strided_array_view;
            strided_array_view<float> view{&array_data[0].float_field, sizeof(test_struct), array_data.size()};

            THEN("view.begin() returns the first element") { CHECK(*view.begin() == 42.0); }

            THEN("view.end() - 1 returns the last element") { CHECK(*(std::prev(view.end())) == 45.0); }
        }

        WHEN("strided_array() is called") {
            THEN("can iterate over fields") {
                using hwcpipe::device::ioctl::strided_array;
                size_t idx = 0;
                for (const auto float_field :
                     &array_data[0].float_field | strided_array(sizeof(test_struct), array_data.size())) {
                    CHECK(float_field == array_data[idx].float_field);
                    ++idx;
                }
            }
        }
    }
}
