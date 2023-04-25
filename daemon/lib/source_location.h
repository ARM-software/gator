/* Copyright (C) 2022-2023 by Arm Limited. All rights reserved. */

#pragma once

#include <cstdint>
#include <string_view>

// unfortunately the version of boost assert used by gator by default is too old and doesnt know about __builtin_XXX, define some better macros here, and we only care about file and line anyway
// otherwise use as default argument leads to  error: predefined identifier is only valid inside function [-Werror,-Wpredefined-identifier-outside-function] :-(
#if defined(__clang__) && (__clang_major__ >= 9)
#define SOURCE_LOCATION                                                                                                \
    ::lib::source_loc_t                                                                                                \
    {                                                                                                                  \
        __builtin_FILE(), __builtin_LINE()                                                                             \
    }
#elif defined(__GNUC__) && (__GNUC__ >= 7)
#define SOURCE_LOCATION                                                                                                \
    ::lib::source_loc_t                                                                                                \
    {                                                                                                                  \
        __builtin_FILE(), __builtin_LINE()                                                                             \
    }
#else
#define SOURCE_LOCATION                                                                                                \
    ::lib::source_loc_t                                                                                                \
    {                                                                                                                  \
        __FILE__, __LINE__                                                                                             \
    }
#endif

#define SLOC_DEFAULT_ARGUMENT ::lib::source_loc_t sloc = SOURCE_LOCATION

namespace lib {
    namespace detail {

        static_assert(std::string_view(__FILE__).rfind(std::string_view("lib/source_location.h"))
                      != std::string_view::npos);

        /** Get the length of the file path prefix for this header (as it is in the source root directory) */
        static constexpr std::size_t file_prefix_len =
            (std::string_view(__FILE__).size() - std::string_view("lib/source_location.h").size());

        /** Some compile time magic to strip out the common file path prefix from some __FILE__ string as passed by one of the LOG_ITEM macros */
        constexpr std::string_view strip_file_prefix(std::string_view str)
        {
            for (std::size_t i = 0; i < file_prefix_len; ++i) {
                if (str[i] != __FILE__[i]) {
                    return str;
                }
                if (str[i] == '\0') {
                    return str;
                }
            }
            return str.substr(file_prefix_len);
        }
    }

    /** Source location identifier */
    class source_loc_t {
    public:
        constexpr source_loc_t() = default;
        constexpr source_loc_t(std::string_view file, std::uint32_t line)
            : file(detail::strip_file_prefix(file)), line(line)
        {
        }

        [[nodiscard]] constexpr std::string_view file_name() const { return file; }
        [[nodiscard]] constexpr std::uint32_t line_no() const { return line; }

    private:
        std::string_view file {};
        std::uint32_t line {};
    };
}
