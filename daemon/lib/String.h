/* Copyright (C) 2018-2022 by Arm Limited. All rights reserved. */

#pragma once

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>

#include <boost/lexical_cast/try_lexical_convert.hpp>

namespace lib {
    /**
     * Like strdup but returns nullptr if the input is null
     */
    inline char * strdup_null(const char * s)
    {
        if (s == nullptr) {
            return nullptr;
        }
        return ::strdup(s);
    }

    /**
     * A helpful class that wraps a buffer + vsnprintf so that the buffer may be printed to in a safe manner
     *
     * @tparam N The buffer size
     */
    template<std::size_t N>
    class printf_str_t {
    public:
        static_assert(N >= 1);

        constexpr printf_str_t() = default;

        [[gnu::format(printf, 2, 3)]] inline printf_str_t(char const * format, ...)
        {
            va_list args;
            va_start(args, format);
            vprintf(format, args);
            va_end(args);
        }

        [[gnu::format(printf, 2, 3)]] inline void printf(char const * format, ...)
        {
            va_list args;
            va_start(args, format);
            vprintf(format, args);
            va_end(args);
        }

        [[gnu::format(printf, 2, 0)]] inline void vprintf(char const * format, va_list args)
        {
            auto n = vsnprintf(buffer.data(), buffer.size() - 1, format, args);

            // make sure to terminate
            length = (n > 0 ? (std::size_t(n) < buffer.size() ? n : N - 1) : 0);
            buffer[length] = 0;
        }

        // NOLINTNEXTLINE(hicpp-explicit-conversions) - implicit conversion is intended
        [[nodiscard]] operator char const *() const { return buffer.data(); }
        // NOLINTNEXTLINE(hicpp-explicit-conversions) - implicit conversion is intended
        [[nodiscard]] operator std::string_view() const { return std::string_view(buffer.data(), length); }

        [[nodiscard]] char const * c_str() const { return buffer.data(); }
        [[nodiscard]] std::size_t size() const { return length; }

    private:
        std::array<char, N> buffer {};
        std::size_t length {};
    };

    class dyn_printf_str_t {
    public:
        inline dyn_printf_str_t() = default;

        [[gnu::format(printf, 2, 3)]] inline dyn_printf_str_t(char const * format, ...)
        {
            va_list args;
            va_start(args, format);
            vprintf(format, args);
            va_end(args);
        }

        [[gnu::format(printf, 2, 3)]] inline void printf(char const * format, ...)
        {
            va_list args;
            va_start(args, format);
            vprintf(format, args);
            va_end(args);
        }

        [[gnu::format(printf, 2, 0)]] inline void vprintf(char const * format, va_list args)
        {
            std::size_t length {};

            // determine the buffer length
            {
                va_list copy;
                va_copy(copy, args);
                auto n = vsnprintf(nullptr, 0, format, copy);
                va_end(copy);

                length = (n > 0 ? std::size_t(n) : 0);
            }

            // resize to fix
            buffer.resize(length);

            // print
            if (length > 0) {
                vsnprintf(buffer.data(), buffer.size() + 1, format, args);
            }
        }

        // NOLINTNEXTLINE(hicpp-explicit-conversions) - implicit conversion is intended
        [[nodiscard]] operator char const *() const { return buffer.c_str(); }
        // NOLINTNEXTLINE(hicpp-explicit-conversions) - implicit conversion is intended
        [[nodiscard]] operator std::string_view() const { return buffer; }
        // NOLINTNEXTLINE(hicpp-explicit-conversions) - implicit move conversion is intended
        [[nodiscard]] operator std::string() && { return std::move(buffer); }

        [[nodiscard]] char const * c_str() const { return buffer.c_str(); }
        [[nodiscard]] std::size_t size() const { return buffer.size(); }
        [[nodiscard]] std::string release() { return std::move(buffer); }

    private:
        std::string buffer {};
    };

    /** Try to parse an int from a string view */
    template<typename T>
    constexpr std::optional<T> try_to_int(std::string_view s)
    {
        T value {0};

        if (!boost::conversion::try_lexical_convert(s, value)) {
            return {};
        }

        return value;
    }

    /** Convert a string_view to an int */
    template<typename T>
    constexpr auto to_int(std::string_view str)
    {
        T result {0};

        if (!boost::conversion::try_lexical_convert(str, result)) {
            throw std::invalid_argument(std::string(str));
        }

        return result;
    }

    /** Convert a string_view to an int, with default on failure */
    template<typename T>
    constexpr auto to_int(std::string_view str, T dflt)
    {
        T result {0};

        if (!boost::conversion::try_lexical_convert(str, result)) {
            return dflt;
        }

        return result;
    }

    /** Does a string_view start with some prefix */
    inline bool starts_with(std::string_view str, std::string_view prefix) { return (str.rfind(prefix, 0) == 0); }
}
