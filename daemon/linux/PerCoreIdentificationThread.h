/* Copyright (C) 2018-2022 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_PER_CORE_IDENTIFICATION_THREAD_H
#define INCLUDE_LINUX_PER_CORE_IDENTIFICATION_THREAD_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <set>
#include <thread>

class PerCoreIdentificationThread {
public:
    static constexpr unsigned INVALID_CORE_ID = ~0U;
    static constexpr unsigned INVALID_PACKAGE_ID = ~0U;
    static constexpr std::uint64_t INVALID_MIDR_EL1 = ~0ULL;

    struct properties_t {
        unsigned core_id;
        unsigned physical_package_id;
        std::set<int> core_siblings;
        std::uint64_t midr_el1;
    };

    /**
     * Consumer function that takes sync event data:
     */
    using ConsumerFunction = std::function<void(unsigned /* cpu_no */, properties_t && /* properties */)>;

    /**
     * Just read and return the detected properties for the specified core, without any attempt to turn on
     */
    static properties_t detectFor(unsigned cpu);

    /**
     * Constructor
     *
     * @param ignoreOffline True if we should ignore cores that are offline (i.e. not try to force them online)
     * @param cpu The number of the CPU the thread is affined to
     * @param consumerFunction The data consumer function
     */
    PerCoreIdentificationThread(bool ignoreOffline, unsigned cpu, ConsumerFunction consumerFunction);

    /** Destructor, waits for thread to terminate */
    ~PerCoreIdentificationThread();

private:
    static void launch(PerCoreIdentificationThread *) noexcept;

    void run() noexcept;
    bool configureAffinity();

    ConsumerFunction consumerFunction;
    std::atomic<bool> terminatedFlag {false};
    unsigned cpu;
    bool ignoreOffline;
    std::thread thread;
};

#endif /* INCLUDE_LINUX_PER_CORE_IDENTIFICATION_THREAD_H */
