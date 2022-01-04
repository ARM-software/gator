/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

// Define to get format macros from inttypes.h
#define __STDC_FORMAT_MACROS

#include "NetDriver.h"

#include "Logging.h"
#include "SessionData.h"

#include <cinttypes>

#include <unistd.h>

class NetCounter : public DriverCounter {
public:
    NetCounter(DriverCounter * next, const char * name, uint64_t * value);

    // Intentionally unimplemented
    NetCounter(const NetCounter &) = delete;
    NetCounter & operator=(const NetCounter &) = delete;
    NetCounter(NetCounter &&) = delete;
    NetCounter & operator=(NetCounter &&) = delete;

    int64_t read() override;

private:
    uint64_t * const mValue;
    uint64_t mPrev;
};

NetCounter::NetCounter(DriverCounter * next, const char * const name, uint64_t * const value)
    : DriverCounter(next, name), mValue(value), mPrev(0)
{
}

int64_t NetCounter::read()
{
    int64_t result = *mValue - mPrev;
    mPrev = *mValue;
    return result;
}

void NetDriver::readEvents(mxml_node_t * const /*unused*/)
{
    if (access("/proc/net/dev", R_OK) == 0) {
        setCounters(new NetCounter(getCounters(), "Linux_net_rx", &mReceiveBytes));
        setCounters(new NetCounter(getCounters(), "Linux_net_tx", &mTransmitBytes));
    }
    else {
        LOG_SETUP("Linux counters\nCannot access /proc/net/dev. Network transmit and receive counters not available.");
    }
}

bool NetDriver::doRead()
{
    if (!countersEnabled()) {
        return true;
    }

    if (!mBuf.read("/proc/net/dev")) {
        return false;
    }

    // Skip the header
    char * key;
    if (((key = strchr(mBuf.getBuf(), '\n')) == nullptr) || ((key = strchr(key + 1, '\n')) == nullptr)) {
        return false;
    }
    key = key + 1;

    mReceiveBytes = 0;
    mTransmitBytes = 0;

    char * colon;
    while ((colon = strchr(key, ':')) != nullptr) {
        char * end = strchr(colon + 1, '\n');
        if (end != nullptr) {
            *end = '\0';
        }
        *colon = '\0';

        uint64_t receiveBytes;
        uint64_t transmitBytes;

        // NOLINTNEXTLINE(cert-err34-c)
        const int count = sscanf(colon + 1, //
                                 " %" SCNu64 " %*u %*u %*u %*u %*u %*u %*u %" SCNu64,
                                 &receiveBytes,
                                 &transmitBytes);
        if (count != 2) {
            return false;
        }
        mReceiveBytes += receiveBytes;
        mTransmitBytes += transmitBytes;

        if (end == nullptr) {
            break;
        }
        key = end + 1;
    }

    return true;
}

void NetDriver::start()
{
    if (!doRead()) {
        LOG_ERROR("Unable to read network stats");
        handleException();
    }
    // Initialize previous values
    for (DriverCounter * counter = getCounters(); counter != nullptr; counter = counter->getNext()) {
        if (!counter->isEnabled()) {
            continue;
        }
        counter->read();
    }
}

void NetDriver::read(IBlockCounterFrameBuilder & buffer)
{
    if (!doRead()) {
        LOG_ERROR("Unable to read network stats");
        handleException();
    }
    super::read(buffer);
}
