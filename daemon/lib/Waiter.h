/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_WAITER_H
#define INCLUDE_LIB_WAITER_H

#include <mutex>
#include <condition_variable>

namespace lib
{
    class Waiter
    {
    public:
        /**
         * Waits until this is disabled
         */
        void wait() const
        {
            std::unique_lock<std::mutex> lock {mutex};
            cv.wait(lock, [&] {return !enabled;});
        }

        /**
         * Waits until a specific time or this is disabled
         *
         * @param timeout_time
         * @return true if waited till the specific time, false if disabled
         */
        template< class Clock, class Duration >
        bool wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) const
        {
            std::unique_lock<std::mutex> lock {mutex};
            return !cv.wait_until(lock, timeout_time, [&] {return !enabled;});
        }

        /**
         * Waits for a specific time or until this is disabled
         *
         * @param timeout_duration
         * @return true if waited for the specific time, false if disabled
         */
        template<class Rep, class Period>
        bool wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) const
        {
            std::unique_lock<std::mutex> lock {mutex};
            return !cv.wait_for(lock, timeout_duration, [&] {return !enabled;});
        }

        bool is_enabled() {
            std::lock_guard<std::mutex> guard {mutex};
            return enabled;
        }

        bool enable() {
            std::lock_guard<std::mutex> guard {mutex};
            const bool prev = enabled;
            enabled = true;
            return prev;
        }

        /**
         * Disables waiting, causing all wait* calls to return
         *
         * @return true if it was enabled beforehand
         */
        bool disable() {
            bool prev;
            {
                std::lock_guard<std::mutex> guard {mutex};
                prev = enabled;
                enabled = false;
            }
            cv.notify_all();
            return prev;
        }

    private:
        bool enabled {true};
        mutable std::mutex mutex {};
        mutable std::condition_variable cv {};
    };
}

#endif // INCLUDE_LIB_WAITER_H

