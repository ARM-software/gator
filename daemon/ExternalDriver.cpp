/* Copyright (C) 2010-2021 by Arm Limited. All rights reserved. */

#include "ExternalDriver.h"

#include "BufferUtils.h"
#include "Logging.h"
#include "OlySocket.h"
#include "SessionData.h"
#include "lib/FileDescriptor.h"

#include <cstdio>

#include <unistd.h>

static const char MALI_UTGARD_SETUP[] = "\0mali-utgard-setup";
static const char SETUP_VERSION[] = "ANNOTATE_SETUP 1\n";
static const size_t HEADER_SIZE = 1 + sizeof(uint32_t);

#define HEADER_ERROR (char(0x80))
#define HEADER_ACK (char(0x81))
#define HEADER_REQUEST_COUNTERS (char(0x82))
#define HEADER_COUNTERS (char(0x83))
#define HEADER_ENABLE_COUNTERS (char(0x84))
#define HEADER_START (char(0x85))

static uint32_t readLEInt(const char * const buf)
{
    uint32_t v = 0;

    for (size_t i = 0; i < sizeof(v); ++i) {
        v |= uint32_t(buf[i]) << 8 * i;
    }

    return v;
}

static int readPackedInt(const char * const buf, const size_t bufSize, size_t * const pos, uint64_t * const l)
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
        mUds = OlySocket::connect(MALI_UTGARD_SETUP, sizeof(MALI_UTGARD_SETUP));
        if (mUds >= 0 && !lib::writeAll(mUds, SETUP_VERSION, sizeof(SETUP_VERSION) - 1)) {
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

    char * const buf = gSessionData.mSharedData->mMaliUtgardCounters;
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
        LOG_DEBUG("Requested counters; size: %zi", size);
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
            name = strndup(buf + begin, pos - begin);
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

    char buf[1 << 12];
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
    buffer_utils::packInt(buf, pos, NS_PER_S / (gSessionData.mSampleRate == 0 ? 10 : gSessionData.mSampleRate));
    buffer_utils::packInt(buf, pos, gSessionData.mLiveRate);
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
