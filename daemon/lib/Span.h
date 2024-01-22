/* Copyright (C) 2018-2023 by Arm Limited. All rights reserved. */

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
    class Span {
    public:
        using value_type = std::remove_cv_t<T>;
        using size_type = L;
        using difference_type = std::make_signed_t<L>;

        using reference = T &;
        using const_reference = const T &;
        using iterator = T *;
        using const_iterator = const T *;

        constexpr L size() const { return length; }

        constexpr T & operator[](std::size_t pos) const
        {
            assert(pos < length);
            return pointer[pos];
        }

        constexpr bool operator==(const Span<T, L> & other) const
        {
            return std::equal(begin(), end(), other.begin(), other.end());
        }

        constexpr Span() = default;

        /// convert Span<T> -> Span<const T>
        template<typename U,
                 typename M,
                 typename = std::enable_if_t<std::is_same_v<value_type, U> && std::is_convertible_v<M, L>>>
        //NOLINTNEXTLINE(hicpp-explicit-conversions)
        constexpr Span(Span<U, M> other) : pointer {other.pointer}, length {other.length}
        {
        }

        constexpr Span(T * data, L length) : pointer {data}, length {length} {}

        template<typename C, //
                 typename = typename C::value_type,
                 typename = typename C::size_type, // make sure is a container
                 // make sure copy constructor is preferred to this
                 typename = std::enable_if_t<!std::is_same_v<std::remove_cv_t<C>, Span>>>
        //NOLINTNEXTLINE(hicpp-explicit-conversions)
        constexpr Span(C & container) : pointer {container.data()}, length {container.size()}
        {
        }

        template<L Size>
        //NOLINTNEXTLINE(hicpp-explicit-conversions)
        constexpr Span(T (&array)[Size]) : pointer {array}, length {Size}
        {
        }

        constexpr Span subspan(size_type offset) const
        {
            assert(offset <= length);
            return {pointer + offset, length - offset};
        }

        constexpr Span subspan(size_type offset, size_type count) const
        {
            assert(offset + count <= length);
            return {pointer + offset, count};
        }

        [[nodiscard]] constexpr T * data() { return pointer; }

        [[nodiscard]] constexpr T const * data() const { return pointer; }

        [[nodiscard]] constexpr iterator begin() { return pointer; }

        [[nodiscard]] constexpr const_iterator begin() const { return pointer; }

        [[nodiscard]] constexpr const_iterator cbegin() const { return pointer; }

        [[nodiscard]] constexpr iterator end() { return pointer + length; }

        [[nodiscard]] constexpr const_iterator end() const { return pointer + length; }

        [[nodiscard]] constexpr const_iterator cend() const { return pointer + length; }

        [[nodiscard]] constexpr bool empty() const { return (pointer == nullptr) || (length <= 0); }

        [[nodiscard]] constexpr reference front()
        {
            assert(!empty());
            return pointer[0];
        }

        [[nodiscard]] constexpr reference back()
        {
            assert(!empty());
            return pointer[length - 1];
        }

        [[nodiscard]] constexpr const_reference front() const
        {
            assert(!empty());
            return pointer[0];
        }

        [[nodiscard]] constexpr const_reference back() const
        {
            assert(!empty());
            return pointer[length - 1];
        }

    private:
        template<typename, typename>
        friend class Span;

        T * pointer = nullptr;
        L length = 0;
    };

    /// Creates a Span object, deducing the value_type from the type of the argument
    template<typename C>
    auto makeSpan(C & container) -> Span<std::remove_pointer_t<decltype(container.data())>, decltype(container.size())>
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

    template<typename C, //
             typename = typename C::value_type,
             typename = typename C::size_type>
    Span(C const &) -> Span<typename C::value_type, typename C::size_type>;
}

#endif // INCLUDE_LIB_SPAN_H
