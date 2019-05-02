/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_MEMORY_H
#define INCLUDE_LIB_MEMORY_H

#include <memory>

namespace lib
{
    /**
     * Creates a unique pointer
     *
     * @param args
     * @return
     */
    template<typename T, typename ... Args>
    std::unique_ptr<T> make_unique(Args&&... args)
    {
        return std::unique_ptr<T> (new T(std::forward<Args>(args)...));
    }

    template<typename T>
    using unique_ptr_void_deleter = std::unique_ptr<T, void (*)(void *)>;

    /**
     * Creates a unique pointer that uses free to delete
     *
     * @param ptr
     * @return
     */
    template<typename T>
    unique_ptr_void_deleter<T> unique_ptr_with_free(T* ptr)
    {
        return unique_ptr_void_deleter<T>(ptr, &::free);
    }
}

#endif // INCLUDE_LIB_MEMORY_H

