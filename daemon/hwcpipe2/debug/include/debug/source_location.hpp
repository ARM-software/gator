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

/**
 * @file source_location.hpp
 * Provides a `<source_location>` compatible source_location structure.
 * See https://en.cppreference.com/w/cpp/experimental/source_location
 */

#pragma once

#include "has_builtin.hpp"

#include <cstdint>

namespace hwcpipe {

namespace debug {

class source_location {
  public:
    constexpr source_location()
        : function_name_(file_name_) {}

    /**
     * @return Source location corresponding to the call site.
     */
    static constexpr source_location current(
#if HWCPIPE_HAS_BUILTIN(__builtin_COLUMN)
        std::uint_least32_t column = __builtin_COLUMN(),
#else
        std::uint_least32_t column = 0,
#endif
        std::uint_least32_t line = __builtin_LINE(), const char *file_name = __builtin_FILE(),
        const char *function_name = __builtin_FUNCTION()) noexcept {
        source_location result;
        result.column_ = column;
        result.line_ = line;
        result.file_name_ = file_name;
        result.function_name_ = function_name;
        return result;
    }

    /**
     * @return Column number represented by this object.
     */
    constexpr std::uint_least32_t column() const noexcept { return column_; }

    /**
     * @return Line number represented by this object.
     */
    constexpr std::uint_least32_t line() const noexcept { return line_; }

    /**
     * @return File name represented by this object.
     */
    constexpr const char *file_name() const noexcept { return file_name_; }

    /**
     * @return Function name represented by this object.
     */
    constexpr const char *function_name() const noexcept { return function_name_; }

  private:
    std::uint_least32_t column_{0U};
    std::uint_least32_t line_{0U};
    const char *file_name_{"unknown"};
    const char *function_name_;
};

} // namespace debug
} // namespace hwcpipe
