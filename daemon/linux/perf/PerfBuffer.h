/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PERF_BUFFER
#define PERF_BUFFER

#include <map>
#include <set>

#include "ClassBoilerPlate.h"
#include "Config.h"

class ISender;

class PerfBuffer
{
public:
    struct Config
    {
        /// must be power of 2
        size_t pageSize;
        /// must be power of 2 multiple of pageSize
        size_t bufferSize;
    };

    PerfBuffer(Config config);
    ~PerfBuffer();

    bool useFd(const int fd, int cpu, bool collectAuxTrace = false);
    void discard(int cpu);
    bool isEmpty();
    bool isFull();
    bool send(ISender & sender);
    std::size_t calculateBufferLength() const;


private:
    Config mConfig;

    struct Buffer
    {
        int fd;
        void * data_buffer;
        void * aux_buffer; // may be null
    };

    std::map<int, Buffer> mBuffers;
    // After the buffer is flushed it should be unmapped
    std::set<int> mDiscard;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(PerfBuffer);
};

/**
 * Validates that config has allowable values and throws exception if not.
 *
 * @param config
 */
void validate(const PerfBuffer::Config & config);

#endif // PERF_BUFFER
