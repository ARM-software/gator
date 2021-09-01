/* Copyright (C) 2019-2021 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_PERF_PERF_CPU_ONLINE_MONITOR_H
#define INCLUDE_LINUX_PERF_PERF_CPU_ONLINE_MONITOR_H

#include <atomic>
#include <functional>
#include <set>
#include <thread>

/**
 * A thread that monitors CPU online / offline state (for when uevents are not available)
 */
class PerfCpuOnlineMonitor {
public:
    /** Notification callback */
    using NotificationCallback = std::function<void(unsigned /* cpu */, bool /* is_online */)>;

    /**
     * Constructor
     *
     * @param callback The function called when a state change occurs
     */
    PerfCpuOnlineMonitor(NotificationCallback callback);
    PerfCpuOnlineMonitor(const PerfCpuOnlineMonitor &) = delete;
    PerfCpuOnlineMonitor(PerfCpuOnlineMonitor &&) = delete;
    ~PerfCpuOnlineMonitor();
    PerfCpuOnlineMonitor & operator=(const PerfCpuOnlineMonitor &) = delete;
    PerfCpuOnlineMonitor & operator=(PerfCpuOnlineMonitor &&) = delete;

    /**
     * Terminate the thread
     */
    void terminate();

private:
    static void launch(PerfCpuOnlineMonitor *) noexcept;

    void run() noexcept;
    void process(bool first, unsigned cpu, bool online);

    std::set<unsigned> onlineCores {};
    NotificationCallback callback;
    std::atomic<bool> terminated {false};
    std::thread thread;
};

#endif /* INCLUDE_LINUX_PERF_PERF_CPU_ONLINE_MONITOR_H */
