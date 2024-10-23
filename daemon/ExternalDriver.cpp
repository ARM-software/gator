/* Copyright (C) 2010-2024 by Arm Limited. All rights reserved. */

#include "ExternalDriver.h"

#include "BufferUtils.h"
#include "Counter.h"
#include "DriverCounter.h"
#include "EventCode.h"
#include "Logging.h"
#include "OlySocket.h"
#include "SessionData.h"
#include "SimpleDriver.h"
#include "Time.h"
#include "lib/Assert.h"
#include "lib/FileDescriptor.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>

namespace {
    constexpr const char * MALI_UTGARD_SETUP = "\0mali-utgard-setup";
    constexpr const char * SETUP_VERSION = "ANNOTATE_SETUP 1\n";
    const size_t HEADER_SIZE = 1 + sizeof(uint32_t);

    constexpr uint8_t HEADER_ACK = 0x81;
    constexpr uint8_t HEADER_REQUEST_COUNTERS = 0x82;
    constexpr uint8_t HEADER_COUNTERS = 0x83;
    constexpr uint8_t HEADER_ENABLE_COUNTERS = 0x84;
    constexpr uint8_t HEADER_START = 0x85;

    uint32_t readLEInt(const uint8_t * const buf)
    {
        uint32_t v = 0;

        for (size_t i = 0; i < sizeof(v); ++i) {
            v |= uint32_t(buf[i]) << 8 * i;
        }

        return v;
    }

    int readPackedInt(const uint8_t * const buf, const size_t bufSize, size_t * const pos, uint64_t * const l)
    {
        uint8_t shift = 0;
        uint8_t b = ~0;

        *l = 0;
        while ((b & 0x80) != 0) {
            if (*pos >= bufSize) {
                return -1;
            }
            b = buf[*pos];
            *pos += 1;
            *l |= uint64_t(b & 0x7f) << shift;
            shift += 7;
        }

        if (shift < 8 * sizeof(*l) && (b & 0x40) != 0) {
            *l |= -(1 << shift);
        }

        return 0;
    }
}

class ExternalCounter : public DriverCounter {
public:
    ExternalCounter(DriverCounter * next, const char * name, int cores) : DriverCounter(next, name), mCores(cores) {}

    // Intentionally undefined
    ExternalCounter(const ExternalCounter &) = delete;
    ExternalCounter & operator=(const ExternalCounter &) = delete;
    ExternalCounter(ExternalCounter &&) = delete;
    ExternalCounter & operator=(ExternalCounter &&) = delete;

    [[nodiscard]] int getCores() const { return mCores; }
    void setEvent(EventCode event) { mEvent = event; }
    [[nodiscard]] EventCode getEvent() const { return mEvent; }

private:
    EventCode mEvent {};
    const int mCores;
};

ExternalDriver::ExternalDriver() : SimpleDriver("External"), mUds(-1), mQueried(false), mStarted(false)
{
}

bool ExternalDriver::connect() const
{
    if (mUds < 0) {
        mUds = OlySocket::connect(MALI_UTGARD_SETUP, strlen(MALI_UTGARD_SETUP) + 1);
        if (mUds >= 0 && !lib::writeAll(mUds, SETUP_VERSION, strlen(SETUP_VERSION))) {
            LOG_ERROR("Unable to send setup version");
            handleException();
        }
    }
    return mUds >= 0;
}

void ExternalDriver::disconnect()
{
    if (mUds >= 0) {
        close(mUds);
        mUds = -1;
        mStarted = false;
    }
}

void ExternalDriver::query() const
{
    if (mQueried) {
        return;
    }
    // Only try once even if it fails otherwise not all the possible counters may be shown
    mQueried = true;

    uint8_t * const buf = gSessionData.mSharedData->mMaliUtgardCounters;
    const size_t bufSize = sizeof(gSessionData.mSharedData->mMaliUtgardCounters);
    size_t size = 0;

    if (!connect()) {
        size = gSessionData.mSharedData->mMaliUtgardCountersSize;
        LOG_DEBUG("Unable to connect, using cached version; size: %zi", size);
    }
    else {
        gSessionData.mSharedData->mMaliUtgardCountersSize = 0;

        buf[0] = HEADER_REQUEST_COUNTERS;
        size_t pos = HEADER_SIZE;
        buffer_utils::writeLEInt(buf + 1, pos);
        if (!lib::writeAll(mUds, buf, pos)) {
            LOG_ERROR("Unable to send request counters message");
            handleException();
        }

        if (!lib::readAll(mUds, buf, HEADER_SIZE) || (buf[0] != HEADER_COUNTERS)) {
            LOG_ERROR("Unable to read request counters response header");
            handleException();
        }
        size = readLEInt(buf + 1);
        if (size > bufSize || !lib::readAll(mUds, buf, size - HEADER_SIZE)) {
            LOG_ERROR("Unable to read request counters response");
            handleException();
        }

        size -= HEADER_SIZE;
        gSessionData.mSharedData->mMaliUtgardCountersSize = size;
        LOG_FINE("Requested counters; size: %zi", size);
    }

    size_t pos = 0;
    while (pos < size) {
        size_t begin = pos;
        char * name = nullptr;
        uint64_t cores = -1;
        while (pos < size && buf[pos] != '\0') {
            ++pos;
        }
        if (pos > begin) {
            name = strndup(reinterpret_cast<const char *>(buf + begin), pos - begin);
        }
        if (pos < size && buf[pos] == '\0') {
            ++pos;
        }
        if (name != nullptr) {
            if (readPackedInt(buf, bufSize, &pos, &cores) == 0) {
                // Cheat so that this can be 'const'
                const_cast<ExternalDriver *>(this)->setCounters(new ExternalCounter(getCounters(), name, cores));
            }
            ::free(name);
        }
    }

    if (pos != size) {
        LOG_ERROR("Unable to parse request counters response");
        handleException();
    }
}

void ExternalDriver::start()
{
    if (!connect()) {
        return;
    }

    if (mStarted) {
        return;
    }
    // Only start once
    mStarted = true;

    uint8_t buf[1 << 12];
    buf[0] = HEADER_ENABLE_COUNTERS;

    int pos = HEADER_SIZE;
    for (auto * counter = static_cast<ExternalCounter *>(getCounters()); counter != nullptr;
         counter = static_cast<ExternalCounter *>(counter->getNext())) {
        if (!counter->isEnabled()) {
            continue;
        }
        size_t nameLen = strlen(counter->getName());
        if (pos + nameLen + 1 + 2 * buffer_utils::MAXSIZE_PACK32 > sizeof(buf)) {
            LOG_ERROR("Unable to enable counters, message is too large");
            handleException();
        }
        memcpy(buf + pos, counter->getName(), nameLen + 1);
        pos += nameLen + 1;
        buffer_utils::packInt(buf, pos, (counter->getEvent().isValid() ? counter->getEvent().asI32() : -1));
        buffer_utils::packInt(buf, pos, counter->getKey());
    }
    buffer_utils::writeLEInt(buf + 1, pos);
    if (!lib::writeAll(mUds, buf, pos)) {
        LOG_ERROR("Unable to send enable counters message");
        handleException();
    }

    if (!lib::readAll(mUds, buf, HEADER_SIZE) || buf[0] != HEADER_ACK) {
        LOG_ERROR("Unable to read enable counters response header");
        handleException();
    }

    if (readLEInt(buf + 1) != HEADER_SIZE) {
        LOG_ERROR("Unable to parse enable counters response");
        handleException();
    }

    buf[0] = HEADER_START;
    pos = HEADER_SIZE;

    // ns/sec / samples/sec = ns/sample
    // For sample rate of none, sample every 100ms
    static constexpr std::uint64_t min_rate = 10UL;

    runtime_assert(gSessionData.mSampleRate != invalid, "Invalid value");

    buffer_utils::packInt(
        buf,
        pos,
        static_cast<int32_t>(
            NS_PER_S
            / (gSessionData.mSampleRate == none ? min_rate : static_cast<std::uint64_t>(gSessionData.mSampleRate))));
    buffer_utils::packInt(buf, pos, static_cast<int32_t>(gSessionData.mLiveRate));
    buffer_utils::writeLEInt(buf + 1, pos);
    if (!lib::writeAll(mUds, buf, pos)) {
        LOG_ERROR("Unable to send start message");
        handleException();
    }

    if (!lib::readAll(mUds, buf, HEADER_SIZE) || buf[0] != HEADER_ACK) {
        LOG_ERROR("Unable to read start response header");
        handleException();
    }

    if (readLEInt(buf + 1) != HEADER_SIZE) {
        LOG_ERROR("Unable to parse start response");
        handleException();
    }
}

bool ExternalDriver::claimCounter(Counter & counter) const
{
    query();
    return super::claimCounter(counter);
}

void ExternalDriver::setupCounter(Counter & counter)
{
    auto * const externalCounter = static_cast<ExternalCounter *>(findCounter(counter));
    if (externalCounter == nullptr) {
        counter.setEnabled(false);
        return;
    }
    externalCounter->setEnabled(true);
    externalCounter->setEvent(counter.getEventCode());
    counter.setKey(externalCounter->getKey());
    if (externalCounter->getCores() > 0) {
        counter.setCores(externalCounter->getCores());
    }
}

void ExternalDriver::resetCounters()
{
    query();
    super::resetCounters();
}
