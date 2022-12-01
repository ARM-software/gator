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

#include <debug/ostream_indent.hpp>

#include <sstream>

SCENARIO("hwcpipe::debug::indent", "[unit]") {
    using hwcpipe::debug::indent;
    using hwcpipe::debug::indent_level;

    GIVEN("stringstream instance") {
        std::stringstream ss;
        WHEN("indent with no level") {
            ss << indent;
            THEN("indent is not applied") { REQUIRE(ss.str() == ""); }
        }

        WHEN("push indent level") {
            ss << indent_level::push;

            THEN("indent expands to 4 spaces when used once") {
                ss << indent;
                REQUIRE(ss.str() == "    ");
            }

            THEN("indent expands to 8 spaces when used twice") {
                ss << indent << indent;
                REQUIRE(ss.str() == "        ");
            }
            AND_THEN("pop indent level") {
                ss << indent_level::pop;

                THEN("indent is not applied") {
                    ss << indent;
                    REQUIRE(ss.str() == "");
                }
            }
        }

        WHEN("indent_guard is used") {
            {
                indent_level::guard g(ss);
                THEN("indent expands to 4 spaces") {
                    ss << indent;
                    REQUIRE(ss.str() == "    ");
                }
            }

            AND_THEN("destructed") {
                THEN("indent is not applied") {
                    ss << indent;
                    REQUIRE(ss.str() == "");
                }
            }
        }
    }
}
