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

#include <debug/source_location.hpp>

#include <cstdint>
#include <string>
#include <tuple>

SCENARIO("hwcpipe::debug::source_location", "[unit]") {
    using hwcpipe::debug::source_location;

    const std::string file_name = __FILE__;
    const std::string function_name = __FUNCTION__;

    /* Make sure that line and location variables are initialized on the same line. */
    auto data = std::make_tuple(source_location::current(), static_cast<std::uint32_t>(__LINE__));

    auto location = std::get<0>(data);
    auto line = std::get<1>(data);

#if HWCPIPE_HAS_BUILTIN(__builtin_COLUMN)
    REQUIRE(location.column() > 0);
#else
    REQUIRE(location.column() == 0);
#endif
    REQUIRE(line == location.line());
    REQUIRE(file_name == location.file_name());
    REQUIRE(function_name == location.function_name());
}
