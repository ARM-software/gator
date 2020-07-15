/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#include "MemInfoDriver.h"

#include "Logging.h"
#include "SessionData.h"

#include <unistd.h>

class MemInfoCounter : public DriverCounter {
public:
    MemInfoCounter(DriverCounter * next, const char * name, int64_t * value);

    int64_t read() override;

private:
    int64_t * const mValue;

    // Intentionally unimplemented
    MemInfoCounter(const MemInfoCounter &) = delete;
    MemInfoCounter & operator=(const MemInfoCounter &) = delete;
    MemInfoCounter(MemInfoCounter &&) = delete;
    MemInfoCounter & operator=(MemInfoCounter &&) = delete;
};

MemInfoCounter::MemInfoCounter(DriverCounter * next, const char * const name, int64_t * const value)
    : DriverCounter(next, name), mValue(value)
{
}

int64_t MemInfoCounter::read()
{
    return *mValue;
}

MemInfoDriver::MemInfoDriver()
    : PolledDriver("MemInfo"), mBuf(), mMemUsed(0), mMemFree(0), mBuffers(0), mCached(0), mSlab(0)
{
}

void MemInfoDriver::readEvents(mxml_node_t * const /*unused*/)
{
    if (access("/proc/meminfo", R_OK) == 0) {
        setCounters(new MemInfoCounter(getCounters(), "Linux_meminfo_memused2", &mMemUsed));
        setCounters(new MemInfoCounter(getCounters(), "Linux_meminfo_memfree", &mMemFree));
        setCounters(new MemInfoCounter(getCounters(), "Linux_meminfo_bufferram", &mBuffers));
        setCounters(new MemInfoCounter(getCounters(), "Linux_meminfo_cached", &mCached));
        setCounters(new MemInfoCounter(getCounters(), "Linux_meminfo_slab", &mSlab));
    }
    else {
        logg.logSetup("Linux counters\nCannot access /proc/meminfo. Memory usage counters not available.");
    }
}

void MemInfoDriver::read(IBlockCounterFrameBuilder & buffer)
{
    if (!countersEnabled()) {
        return;
    }

    if (!mBuf.read("/proc/meminfo")) {
        logg.logError("Failed to read /proc/meminfo");
        handleException();
    }

    char * key = mBuf.getBuf();
    char * colon;
    int64_t memTotal = 0;
    while ((colon = strchr(key, ':')) != nullptr) {
        char * end = strchr(colon + 1, '\n');
        if (end != nullptr) {
            *end = '\0';
        }
        *colon = '\0';

        if (strcmp(key, "MemTotal") == 0) {
            memTotal = strtoll(colon + 1, nullptr, 10) << 10;
        }
        else if (strcmp(key, "MemFree") == 0) {
            mMemFree = strtoll(colon + 1, nullptr, 10) << 10;
        }
        else if (strcmp(key, "Buffers") == 0) {
            mBuffers = strtoll(colon + 1, nullptr, 10) << 10;
        }
        else if (strcmp(key, "Cached") == 0) {
            mCached = strtoll(colon + 1, nullptr, 10) << 10;
        }
        else if (strcmp(key, "Slab") == 0) {
            mSlab = strtoll(colon + 1, nullptr, 10) << 10;
        }

        if (end == nullptr) {
            break;
        }
        key = end + 1;
    }

    mMemUsed = memTotal - mMemFree;

    super::read(buffer);
}
