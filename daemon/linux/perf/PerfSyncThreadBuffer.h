/* Copyright (C) 2018-2021 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_PERF_PERFSYNCTHREADBUFFER_H
#define INCLUDE_LINUX_PERF_PERFSYNCTHREADBUFFER_H

#include "Buffer.h"
#include "linux/perf/PerfSyncThread.h"

#include <cstdint>
#include <semaphore.h>
#include <thread>
#include <unistd.h>
#include <vector>

class ISender;

class PerfSyncThreadBuffer {
public:
    /**
     * Factory method, creates appropriate number of sync thread objects
     *
     * @param supportsClockId True if the kernel perf API supports configuring clock_id
     * @param hasSPEConfiguration True if the user selected at least one SPE configuration
     * @return The buffer object
     */
    static std::unique_ptr<PerfSyncThreadBuffer> create(bool supportsClockId,
                                                        bool hasSPEConfiguration,
                                                        sem_t & senderSem);

    /**
     * Constructor
     *
     * @param enableSyncThreadMode True to enable 'gatord-sync' thread mode
     * @param readTimer True to read the arch timer, false otherwise
     * @param readerSem The buffer reader semaphore
     */
    PerfSyncThreadBuffer(bool enableSyncThreadMode, bool readTimer, sem_t & readerSem);

    /**
     * Start thread
     * @param monotonicRawBase The monotonic raw value that equates to monotonic delta 0
     */
    void start(std::uint64_t monotonicRawBase);

    /**
     * Stop and join thread
     */
    void terminate();

    /**
     * Write buffer to sender
     */
    void send(ISender & sender);

private:
    Buffer buffer;
    PerfSyncThread thread;

    void write(pid_t pid, pid_t tid, std::uint64_t monotonicRaw, std::uint64_t vcnt, std::uint64_t freq);
};

#endif /* INCLUDE_LINUX_PERF_PERFSYNCTHREADBUFFER_H */
