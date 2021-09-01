/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

// Define to get format macros from inttypes.h
#define __STDC_FORMAT_MACROS

#include "DiskIODriver.h"

#include "Logging.h"
#include "SessionData.h"

#include <cinttypes>
#include <unistd.h>

class DiskIOCounter : public DriverCounter {
public:
    DiskIOCounter(DriverCounter * next, const char * name, uint64_t * value);

    // Intentionally unimplemented
    DiskIOCounter(const DiskIOCounter &) = delete;
    DiskIOCounter & operator=(const DiskIOCounter &) = delete;
    DiskIOCounter(DiskIOCounter &&) = delete;
    DiskIOCounter & operator=(DiskIOCounter &&) = delete;

    int64_t read() override;

private:
    uint64_t * const mValue;
    uint64_t mPrev;
};

DiskIOCounter::DiskIOCounter(DriverCounter * next, const char * const name, uint64_t * const value)
    : DriverCounter(next, name), mValue(value), mPrev(0)
{
}

int64_t DiskIOCounter::read()
{
    int64_t result = *mValue - mPrev;
    mPrev = *mValue;
    // Kernel assumes a sector is 512 bytes
    return result << 9;
}

void DiskIODriver::readEvents(mxml_node_t * const /*unused*/)
{
    if (access("/proc/diskstats", R_OK) == 0) {
        setCounters(new DiskIOCounter(getCounters(), "Linux_block_rq_rd", &mReadBytes));
        setCounters(new DiskIOCounter(getCounters(), "Linux_block_rq_wr", &mWriteBytes));
    }
    else {
        logg.logSetup("Linux counters\nCannot access /proc/diskstats. Disk I/O read and write counters not available.");
    }
}

void DiskIODriver::doRead()
{
    if (!countersEnabled()) {
        return;
    }

    if (!mBuf.read("/proc/diskstats")) {
        logg.logError("Unable to read /proc/diskstats");
        handleException();
    }

    mReadBytes = 0;
    mWriteBytes = 0;

    char * lastName = nullptr;
    int lastNameLen = -1;
    char * line = mBuf.getBuf();
    while (*line != '\0') {
        char * end = strchr(line, '\n');
        if (end != nullptr) {
            *end = '\0';
        }

        int nameStart = -1;
        int nameEnd = -1;
        uint64_t readBytes = -1;
        uint64_t writeBytes = -1;
        // NOLINTNEXTLINE(cert-err34-c)
        const int count = sscanf(line,
                                 "%*d %*d %n%*s%n %*u %*u %" SCNu64 " %*u %*u %*u %" SCNu64,
                                 &nameStart,
                                 &nameEnd,
                                 &readBytes,
                                 &writeBytes);
        if (count != 2) {
            logg.logError("Unable to parse /proc/diskstats");
            handleException();
        }

        // Skip partitions which are identified if the name is a substring of the last non-partition
        if ((lastName == nullptr) || (strncmp(lastName, line + nameStart, lastNameLen) != 0)) {
            lastName = line + nameStart;
            lastNameLen = nameEnd - nameStart;
            mReadBytes += readBytes;
            mWriteBytes += writeBytes;
        }

        if (end == nullptr) {
            break;
        }
        line = end + 1;
    }
}

void DiskIODriver::start()
{
    doRead();
    // Initialize previous values
    for (DriverCounter * counter = getCounters(); counter != nullptr; counter = counter->getNext()) {
        if (!counter->isEnabled()) {
            continue;
        }
        counter->read();
    }
}

void DiskIODriver::read(IBlockCounterFrameBuilder & buffer)
{
    doRead();
    super::read(buffer);
}
