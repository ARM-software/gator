/* Copyright (C) 2020-2024 by Arm Limited. All rights reserved. */

#pragma once

#include <cassert>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace lib {
    /**
     * A vector with fixed capacity that doesn't allocate like std::vector
     * but supports adding and removing element unlike std::array
     *
     * @tparam T the type of elements
     * @tparam N the fixed capacity
     */
    template<typename T, std::size_t N>
    class StaticVector {
    public:
        using value_type = T;
        using size_type = std::size_t;

        StaticVector() = default;

        // to be implemented when needed along with other constructors
        StaticVector(const StaticVector & other) = delete;
        StaticVector(StaticVector && other) = delete;
        StaticVector & operator=(const StaticVector & other) = delete;
        StaticVector & operator=(StaticVector && other) = delete;

        ~StaticVector() { clear(); }

        // Element access

        T & operator[](std::size_t pos)
        {
            check(pos);
            return data()[pos];
        }
        const T & operator[](std::size_t pos) const
        {
            check(pos);
            return data()[pos];
        }

        T * data() { return reinterpret_cast<T *>(data_); }
        const T * data() const { return reinterpret_cast<const T *>(data_); }

        // Capacity

        [[nodiscard]] bool empty() const { return size_ == 0; }

        [[nodiscard]] bool full() const { return size_ == N; }

        [[nodiscard]] size_type size() const { return size_; }

        [[nodiscard]] constexpr size_type capacity() const { return N; }

        // Modifiers

        /**
         * Adds an element to the end. Undefined if full.
         */
        void push_back(const T & value)
        {
            check();
            new (data() + size_) T(value);
            ++size_;
        }

        /**
         * Adds an element to the end. Undefined if full.
         */
        void push_back(T && value)
        {
            check();
            new (data() + size_) T(std::move(value));
            ++size_;
        }

        /**
         * Constructs an element in-place end. Undefined if full.
         */
        template<class... Args>
        T & emplace_back(Args &&... args)
        {
            check();
            new (data() + size_) T(std::forward(args...));
            ++size_;
        }

        /**
         * Removes the last element. Undefined if empty.
         */
        void pop_back()
        {
            --size_;
            check();
            data()[size_].~T();
        }

        /**
         * Clears the contents.
         */
        void clear()
        {
            while (!empty()) {
                pop_back();
            }
        }

    private:
        void check(std::size_t pos) const
        {
            assert(pos < N && "StaticVector out of bounds");
            (void) pos; // if assertions are disabled
        }
        void check() const { check(size_); }
        std::aligned_storage_t<sizeof(T), alignof(T)> data_[N];
        std::size_t size_ = 0;
    };

    template<typename T, std::size_t N>
    T * begin(StaticVector<T, N> & staticVector)
    {
        return staticVector.data();
    }
    template<typename T, std::size_t N>
    const T * begin(const StaticVector<T, N> & staticVector)
    {
        return staticVector.data();
    }

    template<typename T, std::size_t N>
    T * end(StaticVector<T, N> & staticVector)
    {
        return staticVector.data() + staticVector.size();
    }
    template<typename T, std::size_t N>
    const T * end(const StaticVector<T, N> & staticVector)
    {
        return staticVector.data() + staticVector.size();
    }
}
