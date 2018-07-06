/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_SHARED_MEMORY_H
#define INCLUDE_SHARED_MEMORY_H

#include <sys/mman.h>

#include <memory>
#include <functional>

namespace shared_memory
{

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
    template<typename T, typename ... Args>
    std::unique_ptr<T, std::function<void(T*)>> make_unique(Args&&... args)
    {
        T * const allocation = allocate<T>(1);

        const std::function<void(T*)> uninitialized_deleter = [](T *p) {
            deallocate<T>(p, 1);
        };

        // The construction could throw so we use a temporary unique pointer to hold the allocation
        // that won't try to destruct the uninitialized allocation
        std::unique_ptr<T, std::function<void(T*)>> uninitialized_ptr { allocation, uninitialized_deleter };

        new (allocation) T(std::forward<Args>(args)...);

        const std::function<void(T*)> initialized_deleter = [](T *p) {
            ::operator delete(p);
            deallocate<T>(p, 1);
        };

        return std::unique_ptr<T, std::function<void(T*)>> { uninitialized_ptr.release(), initialized_deleter };
    }
}

#endif // INCLUDE_SHARED_MEMORY_H

