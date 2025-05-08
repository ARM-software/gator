/* Copyright (C) 2018-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

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

        [[gnu::format(printf, 2, 3)]] printf_str_t(char const * format, ...) // NOLINT(cert-dcl50-cpp)
        {
            va_list args;
            va_start(args, format);
            vprintf(format, args);
            va_end(args);
        }

        [[gnu::format(printf, 2, 3)]] void printf(char const * format, ...) // NOLINT(cert-dcl50-cpp)
        {
            va_list args;
            va_start(args, format);
            vprintf(format, args);
            va_end(args);
        }

        [[gnu::format(printf, 2, 0)]] void vprintf(char const * format, va_list args)
        {
            auto required_length = vsnprintf(buffer.data(), buffer.size(), format, args);

            // update length and null-terminate
            length = std::clamp<size_t>(required_length, 0, N - 1);
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
        dyn_printf_str_t() = default;

        [[gnu::format(printf, 2, 3)]] dyn_printf_str_t(char const * format, ...) // NOLINT(cert-dcl50-cpp)
        {
            va_list args;
            va_start(args, format);
            vprintf(format, args);
            va_end(args);
        }

        [[gnu::format(printf, 2, 3)]] void printf(char const * format, ...) // NOLINT(cert-dcl50-cpp)
        {
            va_list args;
            va_start(args, format);
            vprintf(format, args);
            va_end(args);
        }

        [[gnu::format(printf, 2, 0)]] void vprintf(char const * format, va_list args)
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
                const int needed_len = vsnprintf(buffer.data(), buffer.size() + 1, format, args);
                if (needed_len < 0) {
                    LOG_DEBUG("Encoding error processing: %s", format);
                }
                else if (static_cast<std::size_t>(needed_len) > length) {
                    LOG_DEBUG("Not enough space to expand: %s", format);
                }
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
        std::string buffer;
    };

    /** Try to parse an int from a string view */
    template<typename T>
    constexpr std::optional<T> try_to_int(std::string_view s)
    {
        T value {0};
        if (!boost::conversion::try_lexical_convert(s, value)) {
            return std::nullopt;
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
    inline bool starts_with(std::string_view str, std::string_view prefix)
    {
        return (str.rfind(prefix, 0) == 0);
    }

    /** Does a string_view start with some prefix */
    inline bool ends_with(std::string_view str, std::string_view prefix)
    {
        if (str.size() < prefix.size()) {
            return false;
        }
        return str.substr(str.size() - prefix.size()) == prefix;
    }

    /**
     * Extracts comma separated numbers from a string.
     * @return vector of the numbers in the order they were in the stream, empty on parse error
     */
    template<typename IntType>
    [[nodiscard]] static std::optional<std::vector<IntType>> parseCommaSeparatedNumbers(std::string_view string)
    {
        std::vector<IntType> ints {};

        while (!string.empty()) {
            auto const comma = string.find_first_of(',');
            auto part = string.substr(0, comma);

            part.remove_prefix(std::min(part.find_first_not_of(' '), part.size()));
            part.remove_suffix(part.size() - std::min(part.find(' '), part.size()));

            if (!part.empty()) {
                IntType result {0};

                if (!boost::conversion::try_lexical_convert(part, result)) {
                    return std::nullopt;
                }

                ints.emplace_back(result);
            }

            if (comma == std::string_view::npos) {
                break;
            }

            string = string.substr(comma + 1);
        }

        return {std::move(ints)};
    }
}
