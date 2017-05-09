/* Copyright (c) 2017 by ARM Limited. All rights reserved. */

#ifndef INCLUDE_LIB_THREAD_H
#define INCLUDE_LIB_THREAD_H

#include <atomic>
#include <pthread.h>

namespace lib
{
    class Thread
    {
    public:

        typedef ::pthread_t native_type;

        Thread();
        virtual ~Thread();

        void* join();
        void start();

        bool is_exited() const
        {
            return exited.load(std::memory_order_acquire);
        }

        bool is_join_requested() const
        {
            return join_requested.load(std::memory_order_acquire);
        }

        bool is_running() const
        {
            return running.load(std::memory_order_acquire) && !is_exited();
        }

        bool is_started() const
        {
            return started.load(std::memory_order_acquire);
        }

        native_type get_native_handle() const
        {
            return handle;
        }

    protected:

        virtual void * run() = 0;

    private:

        std::atomic<bool> exited;
        std::atomic<bool> join_requested;
        std::atomic<bool> running;
        std::atomic<bool> started;

        native_type handle;

        static void * staticEntryPoint(void *) noexcept;

        void * entryPoint() noexcept;
    };
}

#endif /* INCLUDE_LIB_THREAD_H */
