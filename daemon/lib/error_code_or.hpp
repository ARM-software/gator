/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include <utility>
#include <variant>

#include <boost/system/error_code.hpp>

namespace lib {
    /** An error code, or some value */
    template<typename T, typename E = boost::system::error_code>
    using error_code_or_t = std::variant<E, T>;

    /** @return The error code, or nullptr if no error */
    template<typename T, typename E>
    constexpr auto * get_error(error_code_or_t<T, E> const & eot)
    {
        return std::get_if<E>(&eot);
    }

    /** @return The value, must previously have been checked for no error */
    template<typename T, typename E>
    constexpr T & get_value(error_code_or_t<T, E> & eot)
    {
        return std::get<T>(eot);
    }

    /** @return The value, must previously have been checked for no error */
    template<typename T, typename E>
    constexpr T const & get_value(error_code_or_t<T, E> const & eot)
    {
        return std::get<T>(eot);
    }

    /** @return The value, must previously have been checked for no error */
    template<typename T, typename E>
    constexpr T && get_value(error_code_or_t<T, E> && eot)
    {
        return std::move(std::get<T>(std::move(eot)));
    }

    /** Copy either the error or the value into one of the provided arguments.
     * @return True if the value was extracted, false if the error was
     */
    template<typename T, typename E>
    constexpr bool get_error_or_value(error_code_or_t<T, E> const & eot, T & value, E & error)
    {
        if (auto const * e = get_error(eot)) {
            error = *e;
            return false;
        }

        value = get_value(eot);
        return true;
    }

    /** Move either the error or the value into one of the provided arguments.
     * @return True if the value was extracted, false if the error was
     */
    template<typename T, typename E>
    constexpr bool get_error_or_value(error_code_or_t<T, E> && eot, T & value, E & error)
    {
        if (auto const * e = get_error(eot)) {
            error = *e;
            return false;
        }

        value = get_value(std::move(eot));
        return true;
    }
}
