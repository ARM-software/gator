/* Copyright (C) 2018-2021 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_PERF_PERFSYNCTHREAD_H
#define INCLUDE_LINUX_PERF_PERFSYNCTHREAD_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

class PerfSyncThread {
public:
    /**
     * Consumer function that takes sync event data:
     *
     * The first argument is the sync thread's PID
     * The second argument is the sync thread's TID
     * The third argument is the CNTFREQ_EL0
     * The fourth argument is the current value of CLOCK_MONOTONIC_RAW
     * The fifth argument is the current value of CNTVCT_EL0
     */
    using ConsumerFunction = std::function<void(pid_t, pid_t, std::uint64_t, std::uint64_t, std::uint64_t)>;

    /**
     * Constructor
     * @param enableSyncThreadMode True to enable 'gatord-sync' thread mode
     * @param readTimer True to read the arch timer, false otherwise
     * @param consumerFunction The data consumer function
     */
    PerfSyncThread(bool enableSyncThreadMode, bool readTimer, ConsumerFunction consumerFunction);

    ~PerfSyncThread();

    /**
     * Start the thread
     */
    void start(std::uint64_t monotonicRawBase);

    /**
     * Terminate the thread
     */
    void terminate();

private:
    void run(std::uint64_t monotonicRawBase) noexcept;

    void rename(std::uint64_t currentTime) const;

    std::thread thread;
    ConsumerFunction consumerFunction;
    std::atomic_bool terminateFlag;
    bool readTimer;
    bool enableSyncThreadMode;
};

#endif /* INCLUDE_LINUX_PERF_PERFSYNCTHREAD_H */
