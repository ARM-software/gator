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

class Sender;

class PerfBuffer
{
public:

    static std::size_t calculateBufferLength();
    static std::size_t calculateMMapLength();

    PerfBuffer();
    ~PerfBuffer();

    bool useFd(const int fd, int cpu);
    void discard(int cpu);
    bool isEmpty();
    bool isFull();
    bool send(Sender * const sender);

private:
    struct Buffer
    {
        int fd;
        void * buffer;
    };

    std::map<int, Buffer> mBuffers;
    // After the buffer is flushed it should be unmapped
    std::set<int> mDiscard;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(PerfBuffer);
};

#endif // PERF_BUFFER
