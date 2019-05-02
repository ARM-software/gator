/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "ExternalDriver.h"

#include <stdio.h>
#include <unistd.h>

#include "BufferUtils.h"
#include "Logging.h"
#include "OlySocket.h"
#include "SessionData.h"

#include "lib/FileDescriptor.h"

static const char MALI_UTGARD_SETUP[] = "\0mali-utgard-setup";
static const char SETUP_VERSION[] = "ANNOTATE_SETUP 1\n";
static const size_t HEADER_SIZE = 1 + sizeof(uint32_t);

#define HEADER_ERROR            (char(0x80))
#define HEADER_ACK              (char(0x81))
#define HEADER_REQUEST_COUNTERS (char(0x82))
#define HEADER_COUNTERS         (char(0x83))
#define HEADER_ENABLE_COUNTERS  (char(0x84))
#define HEADER_START            (char(0x85))

static uint32_t readLEInt(char * const buf)
{
    size_t i;
    uint32_t v;

    v = 0;
    for (i = 0; i < sizeof(v); ++i)
        v |= uint32_t(buf[i]) << 8 * i;

    return v;
}

static int readPackedInt(char * const buf, const size_t bufSize, size_t * const pos, uint64_t * const l)
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

class ExternalCounter : public DriverCounter
{
public:
    ExternalCounter(DriverCounter *next, const char *name, int cores)
            : DriverCounter(next, name),
              mCores(cores),
              mEvent(-1)
    {
    }

    ~ExternalCounter()
    {
    }

    int getCores() const
    {
        return mCores;
    }
    void setEvent(const int event)
    {
        mEvent = event;
    }
    int getEvent() const
    {
        return mEvent;
    }

private:
    const int mCores;
    int mEvent;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(ExternalCounter);
};

ExternalDriver::ExternalDriver()
        : SimpleDriver("External"),
          mUds(-1),
          mQueried(false),
          mStarted(false)
{
}

bool ExternalDriver::connect() const
{
    if (mUds < 0) {
        mUds = OlySocket::connect(MALI_UTGARD_SETUP, sizeof(MALI_UTGARD_SETUP));
        if (mUds >= 0 && !lib::writeAll(mUds, SETUP_VERSION, sizeof(SETUP_VERSION) - 1)) {
            logg.logError("Unable to send setup version");
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
        logg.logMessage("Unable to connect, using cached version; size: %zi", size);
    }
    else {
        gSessionData.mSharedData->mMaliUtgardCountersSize = 0;

        buf[0] = HEADER_REQUEST_COUNTERS;
        size_t pos = HEADER_SIZE;
        buffer_utils::writeLEInt(buf + 1, pos);
        if (!lib::writeAll(mUds, buf, pos)) {
            logg.logError("Unable to send request counters message");
            handleException();
        }

        if (!lib::readAll(mUds, buf, HEADER_SIZE) || (buf[0] != HEADER_COUNTERS)) {
            logg.logError("Unable to read request counters response header");
            handleException();
        }
        size = readLEInt(buf + 1);
        if (size > bufSize || !lib::readAll(mUds, buf, size - HEADER_SIZE)) {
            logg.logError("Unable to read request counters response");
            handleException();
        }

        size -= HEADER_SIZE;
        gSessionData.mSharedData->mMaliUtgardCountersSize = size;
        logg.logMessage("Requested counters; size: %zi", size);
    }

    size_t pos = 0;
    while (pos < size) {
        size_t begin = pos;
        char *name = NULL;
        uint64_t cores = -1;
        while (pos < size && buf[pos] != '\0') {
            ++pos;
        }
        if (pos > begin) {
            name = strndup(buf + begin, pos - begin);
        }
        if (pos < size && buf[pos] == '\0') {
            ++pos;
        };
        if (name != NULL) {
            if (readPackedInt(buf, bufSize, &pos, &cores) == 0) {
                // Cheat so that this can be 'const'
                const_cast<ExternalDriver *>(this)->setCounters(new ExternalCounter(getCounters(), name, cores));
            }
            ::free(name);
        }
    }

    if (pos != size) {
        logg.logError("Unable to parse request counters response");
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
    int pos;

    buf[0] = HEADER_ENABLE_COUNTERS;
    pos = HEADER_SIZE;
    for (ExternalCounter *counter = static_cast<ExternalCounter *>(getCounters()); counter != NULL;
            counter = static_cast<ExternalCounter *>(counter->getNext())) {
        if (!counter->isEnabled()) {
            continue;
        }
        size_t nameLen = strlen(counter->getName());
        if (pos + nameLen + 1 + 2 * buffer_utils::MAXSIZE_PACK32 > sizeof(buf)) {
            logg.logError("Unable to enable counters, message is too large");
            handleException();
        }
        memcpy(buf + pos, counter->getName(), nameLen + 1);
        pos += nameLen + 1;
        buffer_utils::packInt(buf, pos, counter->getEvent());
        buffer_utils::packInt(buf, pos, counter->getKey());
    }
    buffer_utils::writeLEInt(buf + 1, pos);
    if (!lib::writeAll(mUds, buf, pos)) {
        logg.logError("Unable to send enable counters message");
        handleException();
    }

    if (!lib::readAll(mUds, buf, HEADER_SIZE) || buf[0] != HEADER_ACK) {
        logg.logError("Unable to read enable counters response header");
        handleException();
    }

    if (readLEInt(buf + 1) != HEADER_SIZE) {
        logg.logError("Unable to parse enable counters response");
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
        logg.logError("Unable to send start message");
        handleException();
    }

    if (!lib::readAll(mUds, buf, HEADER_SIZE) || buf[0] != HEADER_ACK) {
        logg.logError("Unable to read start response header");
        handleException();
    }

    if (readLEInt(buf + 1) != HEADER_SIZE) {
        logg.logError("Unable to parse start response");
        handleException();
    }
}

bool ExternalDriver::claimCounter(Counter &counter) const
{
    query();
    return super::claimCounter(counter);
}

void ExternalDriver::setupCounter(Counter &counter)
{
    ExternalCounter * const externalCounter = static_cast<ExternalCounter *>(findCounter(counter));
    if (externalCounter == NULL) {
        counter.setEnabled(false);
        return;
    }
    externalCounter->setEnabled(true);
    counter.setKey(externalCounter->getKey());
    if (counter.getEvent() != -1) {
        externalCounter->setEvent(counter.getEvent());
    }
    if (externalCounter->getCores() > 0) {
        counter.setCores(externalCounter->getCores());
    }
}

void ExternalDriver::resetCounters()
{
    query();
    super::resetCounters();
}
