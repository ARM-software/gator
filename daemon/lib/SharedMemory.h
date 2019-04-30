/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_SHARED_MEMORY_H
#define INCLUDE_SHARED_MEMORY_H

#include <sys/mman.h>

#include <memory>
#include <functional>

#include "Throw.h"

namespace shared_memory
{
    template<typename T>
    using unique_ptr = std::unique_ptr<T, std::function<void(typename std::conditional<std::is_array<T>::value, typename std::remove_extent<T>::type, T>::type*)>>;

    /**
     * Allocates an array of n T in shared memory
     *
     * @param n
     * @return
     */
    template<typename T>
    T* allocate(std::size_t n)
    {
        void * const allocation = mmap(nullptr, sizeof(T) * n, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1,
                                       0);
        if (allocation == MAP_FAILED)
            GATOR_THROW(std::bad_alloc());
        return static_cast<T*>(allocation);
    }

    /**
     * Deallocates an array of n T in shared memory
     *
     * @param n
     * @return
     */
    template<typename T>
    void deallocate(T* p, std::size_t n)
    {
        munmap(p, sizeof(T) * n);
    }

    /**
     * Creates a unique pointer in shared memory
     *
     * @param args
     * @return
     */
    template<typename T, typename ... Args, typename = typename std::enable_if<!std::is_array<T>::value>::type>
    unique_ptr<T> make_unique(Args&&... args)
    {
        T * const allocation = allocate<T>(1);

        const std::function<void(T*)> uninitialized_deleter = [](T *p) {
            deallocate<T>(p, 1);
        };

        // The construction could throw so we use a temporary unique pointer to hold the allocation
        // that won't try to destruct the uninitialized allocation
        unique_ptr<T> uninitialized_ptr { allocation, uninitialized_deleter };

        new (allocation) T(std::forward<Args>(args)...);

        const std::function<void(T*)> initialized_deleter = [](T *p) {
            p->~T();
            deallocate<T>(p, 1);
        };

        return unique_ptr<T> { uninitialized_ptr.release(), initialized_deleter };
    }

    /**
     * Creates a unique pointer for an array in shared memory
     *
     * @return
     */
    template<typename T, typename = typename std::enable_if<std::is_array<T>::value>::type>
    unique_ptr<T> make_unique(std::size_t size)
    {
        using element_type = typename unique_ptr<T>::element_type;
        element_type * const allocation = allocate<element_type>(size);

        std::size_t number_initialized = 0;
        const std::function<void(element_type*)> uninitialized_deleter = [size, &number_initialized](element_type *p) {
            for (std::size_t i = 0; i < number_initialized; ++i) {
                p[i].~element_type();
            }
            deallocate<element_type>(p, size);
        };

        // The construction could throw so we use a temporary unique pointer to hold the allocation
        // that won't try to destruct the uninitialized allocation
        unique_ptr<T> uninitialized_ptr { allocation, uninitialized_deleter };

        for (; number_initialized < size; ++number_initialized) {
            new (allocation + number_initialized) element_type();
        }

        const std::function<void(element_type*)> initialized_deleter = [size](element_type *p) {
            for (std::size_t i = 0; i < size; ++i) {
                p[i].~element_type();
            }
            deallocate<element_type>(p, size);
        };

        return unique_ptr<T> { uninitialized_ptr.release(), initialized_deleter };
    }
}

#endif // INCLUDE_SHARED_MEMORY_H

