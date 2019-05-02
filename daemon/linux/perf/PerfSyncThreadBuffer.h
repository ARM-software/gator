/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_PERF_PERFSYNCTHREADBUFFER_H
#define INCLUDE_LINUX_PERF_PERFSYNCTHREADBUFFER_H

#include <cstdint>
#include <semaphore.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "Buffer.h"
#include "linux/perf/PerfSyncThread.h"

class ISender;

class PerfSyncThreadBuffer
{
public:

    /**
     * Factory method, creates appropriate number of sync thread objects
     *
     * @param monotonicRawBase The monotonic raw value that equates to monotonic delta 0
     * @param supportsClockId True if the kernel perf API supports configuring clock_id
     * @param hasSPEConfiguration True if the user selected at least one SPE configuration
     * @return The list of buffer objects
     */
    static std::vector<std::unique_ptr<PerfSyncThreadBuffer>> create(std::uint64_t monotonicRawBase, bool supportsClockId, bool hasSPEConfiguration, sem_t & senderSem);

    /**
     * Constructor
     *
     * @param cpu The number of the CPU the thread is affined to
     * @param enableSyncThreadMode True to enable 'gatord-sync' thread mode
     * @param readTimer True to read the arch timer, false otherwise
     * @param readerSem The buffer reader semaphore
     */
    PerfSyncThreadBuffer(std::uint64_t monotonicRawBase, unsigned cpu, bool enableSyncThreadMode, bool readTimer, sem_t * readerSem);

    /**
     * Stop thread
     */
    void terminate();

    /**
     * Check complete
     */
    bool complete() const;

    /**
     * Write buffer to sender
     * @param sender
     */
    void send(ISender & sender);

private:

    std::uint64_t monotonicRawBase;
    Buffer buffer;
    PerfSyncThread thread;

    void write(unsigned cpu, pid_t pid, pid_t tid, std::uint64_t monotonicRaw, std::uint64_t vcnt, std::uint64_t freq);
};

#endif /* INCLUDE_LINUX_PERF_PERFSYNCTHREADBUFFER_H */
