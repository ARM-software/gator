/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_PERF_PERFSYNCTHREAD_H
#define INCLUDE_LINUX_PERF_PERFSYNCTHREAD_H

#include <atomic>
#include <functional>
#include <cstdint>
#include <thread>

class PerfSyncThread
{
public:

    /**
     * Consumer function that takes sync event data:
     *
     * The first argument is CPU number
     * The second argument is the sync thread's PID
     * The third argument is the sync thread's TID
     * The fourth argument is the CNTFREQ_EL0
     * The fifth argument is the current value of CLOCK_MONOTONIC_RAW
     * The sixth argument is the current value of CNTVCT_EL0
     */
    using ConsumerFunction = std::function<void(unsigned, pid_t, pid_t, std::uint64_t, std::uint64_t, std::uint64_t)>;

    /**
     * Constructor
     *
     * @param cpu The number of the CPU the thread is affined to
     * @param enableSyncThreadMode True to enable 'gatord-sync' thread mode
     * @param readTimer True to read the arch timer, false otherwise
     * @param monotonicRawBase The base CLOCK_MONOTONIC_RAW considered zero
     * @param consumerFunction The data consumer function
     */
    PerfSyncThread(unsigned cpu, bool enableSyncThreadMode, bool readTimer, std::uint64_t monotonicRawBase, ConsumerFunction consumerFunction);

    ~PerfSyncThread();

    /**
     * Terminate the thread
     */
    void terminate();

private:

    static void launch(PerfSyncThread *) noexcept;

    void run() noexcept;

    void rename(std::uint64_t currentTime);

    std::thread thread;
    ConsumerFunction consumerFunction;
    std::uint64_t monotonicRawBase;
    unsigned cpu;
    std::atomic_bool terminateFlag;
    bool readTimer;
    bool enableSyncThreadMode;
};

#endif /* INCLUDE_LINUX_PERF_PERFSYNCTHREAD_H */
