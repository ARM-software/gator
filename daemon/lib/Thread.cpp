/* Copyright (c) 2017 by ARM Limited. All rights reserved. */

#include "lib/Thread.h"
#include "lib/Assert.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <exception>

namespace lib
{
    Thread::Thread()
            : exited(false),
              join_requested(false),
              running(false),
              started(false),
              handle(0)
    {
    }

    Thread::~Thread()
    {
        join();
    }

    void * Thread::join()
    {
        // mutate join_requested value
        bool join_requested_value = false;
        while ((!join_requested.compare_exchange_weak(join_requested_value, true, std::memory_order_acq_rel)) && (!join_requested_value)) {
            // loop until modified
        }

        void * exit_value = nullptr;

        if (!join_requested_value) {
            // wait for join
            const int result = ::pthread_join(handle, &exit_value);
            if (result != 0) {
                std::fprintf(stderr, "pthread_join returned error code %i (%s)\n", result, std::strerror(result));
                abort();
            }
        }

        while (!is_exited()) {
            // just spin
        }

        return exit_value;
    }

    void Thread::start()
    {
        bool expected = false;

        while ((!started.compare_exchange_weak(expected, true, std::memory_order_acq_rel)) && (!expected)) {
            // loop until modified
        }

        if (!expected) {
            const int result = ::pthread_create(&handle, nullptr, Thread::staticEntryPoint, this);
            if (result != 0) {
                std::fprintf(stderr, "pthread_create returned error code %i (%s)\n", result, std::strerror(result));
                abort();
            }
        }
        else {
            runtime_assert(false, "Thread::start called twice");
        }
    }

    void * Thread::staticEntryPoint(void * this_ptr) noexcept
    {
        Thread * const thread = reinterpret_cast<Thread *>(this_ptr);

        runtime_assert(thread != nullptr, "Thread::staticEntryPoint called with 'this_ptr == nullptr'");

        return thread->entryPoint();
    }

    void * Thread::entryPoint() noexcept
    {
        void * result = nullptr;

        // indicate that thread has started
        running.store(true, std::memory_order_release);

#if defined(__EXCEPTIONS)
        try {
            // invoke the run method
            result = run();
        }
        catch (const std::exception & ex) {
            // log the exception and abort
            std::fprintf(stderr, "Thread::run threw an exception: %s\n", ex.what());
        }
        catch (...) {
            // do not propagate the value
            std::fprintf(stderr, "Thread::run threw an unknown object\n");
        }
#else
        // invoke the run method
        result = run();
#endif

        // indicate that thread is no longer running
        exited.store(true, std::memory_order_release);

        return result;
    }
}
