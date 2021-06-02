/* Copyright (C) 2018-2021 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_MEMORY_H
#define INCLUDE_LIB_MEMORY_H

#include <cstdlib>
#include <memory>

namespace lib {
    /**
     * Creates a unique pointer
     *
     * Can be replaced with std::make_unique in C++14
     */
    template<typename T, typename... Args>
    std::unique_ptr<T> make_unique(Args &&... args)
    {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }

    /**
     * Creates a unique pointer and deduces its type from the argument
     *
     * Can be replaced with std::unique_ptr (CTAD) in C++17
     */
    template<typename T>
    std::unique_ptr<T> unique_ptr(T * ptr)
    {
        return std::unique_ptr<T>(ptr);
    }

    template<typename T>
    using unique_ptr_void_deleter = std::unique_ptr<T, void (*)(void *)>;

    /**
     * Creates a unique pointer that uses free to delete
     */
    template<typename T>
    unique_ptr_void_deleter<T> unique_ptr_with_free(T * ptr)
    {
        return unique_ptr_void_deleter<T>(ptr, &::free);
    }
}

#endif // INCLUDE_LIB_MEMORY_H
