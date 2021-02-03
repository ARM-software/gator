/* Copyright (C) 2018-2020 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_SPAN_H
#define INCLUDE_LIB_SPAN_H

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace lib {
    /**
     * Array length pair
     */
    template<typename T, typename L = std::size_t>
    struct Span {
        using value_type = typename std::remove_cv<T>::type;
        using size_type = L;
        using difference_type = typename std::make_signed<L>::type;

        using reference = T &;
        using const_reference = const T &;
        using iterator = T *;
        using const_iterator = const T *;

        T * data = nullptr;
        L length = 0;

        L size() const { return length; }

        T & operator[](std::size_t pos) const
        {
            assert(pos < length);
            return data[pos];
        }

        bool operator==(const Span<T, L> & other) const
        {
            return std::equal(begin(), end(), other.begin(), other.end());
        }

        Span() = default;

        /// convert Span<T> -> Span<const T>
        template<typename U,
                 typename M,
                 typename = typename std::enable_if<std::is_same<value_type, U>::value &&
                                                    std::is_convertible<M, L>::value>::type>
        Span(Span<U, M> other) : data {other.data}, length {other.length}
        {
        }

        Span(T * data, L length) : data {data}, length {length} {}

        template<typename C, //
                 typename = typename C::value_type,
                 typename = typename C::size_type, // make sure is a container
                 // make sure copy constructor is preferred to this
                 typename = typename std::enable_if<!std::is_same<typename std::remove_cv<C>::type, Span>::value>::type>
        Span(C & container) : data {container.data()}, length {container.size()}
        {
        }

        template<L Size>
        Span(T (&array)[Size]) : data {array}, length {Size}
        {
        }

        Span subspan(size_type offset) const
        {
            assert(offset <= length);
            return {data + offset, length - offset};
        }

        Span subspan(size_type offset, size_type count) const
        {
            assert(offset + count <= length);
            return {data + offset, count};
        }

        iterator begin() { return data; }

        const_iterator begin() const { return data; }

        const_iterator cbegin() const { return data; }

        iterator end() { return data + length; }

        const_iterator end() const { return data + length; }

        const_iterator cend() const { return data + length; }
    };

    /// Creates a Span object, deducing the value_type from the type of the argument
    template<typename C>
    auto makeSpan(C & container)
        -> Span<typename std::remove_pointer<decltype(container.data())>::type, decltype(container.size())>
    {
        return {container.data(), container.size()};
    }

    /// Creates a Span object, deducing the value_type from the type of the argument
    template<typename T, typename L = std::size_t, L Size>
    Span<T, L> makeSpan(T (&array)[Size])
    {
        return Span<T, L> {array, Size};
    }

    /// Creates a Span object, deducing the value_type from the type of the argument
    template<typename C>
    auto makeConstSpan(C & container)
        -> Span<const typename std::remove_pointer<decltype(container.data())>::type, decltype(container.size())>
    {
        return {container.data(), container.size()};
    }

    /// Creates a Span object, deducing the value_type from the type of the argument
    template<typename T, typename L = std::size_t, L Size>
    Span<const T, L> makeConstSpan(T (&array)[Size])
    {
        return Span<const T, L> {array, Size};
    }
}

#endif // INCLUDE_LIB_SPAN_H
