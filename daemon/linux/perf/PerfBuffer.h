/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef PERF_BUFFER
#define PERF_BUFFER

#include "Config.h"
#include "lib/Span.h"
#include "linux/perf/IPerfBufferConsumer.h"

#include <map>
#include <set>
#include <vector>

class PerfBuffer {
public:
    struct Config {
        /// must be power of 2
        size_t pageSize;
        /// must be power of 2 multiple of pageSize
        size_t dataBufferSize;
        /// must be power of 2 multiple of pageSize (or 0)
        size_t auxBufferSize;
    };

    PerfBuffer(Config config);
    ~PerfBuffer();

    bool useFd(int fd, int cpu, bool collectAuxTrace = false);
    void discard(int cpu);
    bool isFull();
    bool send(IPerfBufferConsumer & bufferConsumer);

    std::size_t getDataBufferLength() const;
    std::size_t getAuxBufferLength() const;

private:
    Config mConfig;

    struct Buffer {
        void * data_buffer;
        void * aux_buffer; // may be null
        int fd;
        int aux_fd;
    };

    std::map<int, Buffer> mBuffers;
    // After the buffer is flushed it should be unmapped
    std::set<int> mDiscard;

    // Intentionally undefined
    PerfBuffer(const PerfBuffer &) = delete;
    PerfBuffer & operator=(const PerfBuffer &) = delete;
    PerfBuffer(PerfBuffer &&) = delete;
    PerfBuffer & operator=(PerfBuffer &&) = delete;
};

/**
 * Validates that config has allowable values and throws exception if not.
 *
 * @param config
 */
void validate(const PerfBuffer::Config & config);

#endif // PERF_BUFFER
