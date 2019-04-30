/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "MidgardDriver.h"

#include <unistd.h>
#include <cinttypes>

#include "lib/FileDescriptor.h"

#include "Buffer.h"
#include "Logging.h"
#include "OlySocket.h"
#include "SessionData.h"

static const uint32_t PACKET_SHARED_PARAMETER = 0x0000;
static const uint32_t PACKET_HARDWARE_COUNTER_DIRECTORY = 0x0002;

struct PacketHeader
{
    uint32_t mImplSpec :8, mReserved0 :8, mPacketIdentifier :16; //mPacketId : 10, mPacketFamily : 6;
    uint32_t mDataLength :23, mSequenceNumbered :1, mReserved1 :8;
}__attribute__((packed));

struct SharedParameterPacket
{
    uint32_t mMaliMagic;
    uint32_t mMaxDataLen :24, mReserved2 :8;
    uint32_t mPid;
    uint32_t mOffsets[4];
}__attribute__((packed));

struct HardwareCounter
{
    uint16_t mCounterIndex;
    uint32_t mCounterNameLen;
    char mCounterName[];
}__attribute__((packed));

struct GPUPerfPeriod
{
    uint32_t mDeclId;
    int32_t mMicroseconds;
    uint32_t mStartIndex;
    uint64_t mEnableMap;
}__attribute__((packed));

struct GLESWindump
{
    uint32_t mDeclId;
    int32_t mSkipframes;
    uint32_t mMinWidth;
    uint32_t mMinHeight;
}__attribute__((packed));

struct CounterData
{
    enum
    {
        PERF,
        WINDUMP,
        ACTIVITY,
    } mType;
    union
    {
        struct
        {
            // PERF
            int mIndex;
        };
        struct
        {
            // WINDUMP
        };
        struct
        {
            // ACTIVITY
            int mCores;
        };
    };
};

class MidgardCounter : public DriverCounter
{
public:
    MidgardCounter(DriverCounter *next, const char *name, CounterData * const counterData)
            : DriverCounter(next, name),
              mCounterData(*counterData),
              mEvent(-1)
    {
    }

    ~MidgardCounter()
    {
    }

    int getType() const
    {
        return mCounterData.mType;
    }

    // PERF
    int getIndex() const
    {
        return mCounterData.mIndex;
    }

    // ACTIVITY
    int getCores() const
    {
        return mCounterData.mCores;
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
    const CounterData mCounterData;
    int mEvent;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(MidgardCounter);
};

MidgardDriver::MidgardDriver()
        : SimpleDriver("MidgardDriver"),
          mQueried(false)
{
}

MidgardDriver::~MidgardDriver()
{
}

void MidgardDriver::query() const
{
    if (mQueried) {
        return;
    }
    // Only try once even if it fails otherwise not all the possible counters may be shown
    mQueried = true;

    // Prefer not to requery once obtained as it could throw capture off, assume it doesn't change
    if (gSessionData.mSharedData->mMaliMidgardCountersSize > 0) {
        logg.logMessage("Using cached Midgard counters");
    }
    else {
        int uds = OlySocket::connect(MALI_GRAPHICS, MALI_GRAPHICS_SIZE);
        if (uds < 0) {
            logg.logMessage("Unable to connect to Midgard");
        }
        else {
            logg.logMessage("Connected to midgard");
            gSessionData.mSharedData->mMaliMidgardCountersSize = 0;

            PacketHeader header;
            bool first = true;

            while (true) {
                if (!lib::readAll(uds, &header, sizeof(PacketHeader))) {
                    logg.logError("Unable to read Midgard header");
                    handleException();
                }
                if (first && (reinterpret_cast<const uint8_t *>(&header)[0] != 0)) {
                    logg.logMessage("Midgard data is not in encapsulated format");
                    break;
                }
                first = false;

                if (header.mSequenceNumbered) {
                    logg.logError("sequence_numbered is true and is unsupported");
                    handleException();
                }

                logg.logMessage("MIPE Packet: 0x%x 0x%x 0x%x 0x%x 0x%x",
                                header.mDataLength,
                                header.mImplSpec,
                                header.mPacketIdentifier,
                                header.mReserved0,
                                header.mReserved1);

                switch (header.mPacketIdentifier) {
                case PACKET_SHARED_PARAMETER: {
                    SharedParameterPacket packet;
                    if (header.mDataLength < sizeof(packet)) {
                        logg.logError("Unable to read Shared Parameter Packet because it's at least %zu bytes long but only %" PRIu32 " bytes were given", sizeof(packet), header.mDataLength);
                        handleException();
                    }
                    if (!lib::readAll(uds, &packet, sizeof(packet))) {
                        logg.logError("Unable to read Shared Parameter Packet");
                        handleException();
                    }
                    if (!lib::skipAll(uds, header.mDataLength - sizeof(packet))) {
                        logg.logError("Unable to skip Shared Parameter Packet pool");
                        handleException();
                    }


                    if (header.mImplSpec == 0 && packet.mReserved2 == 0) {
                        if (packet.mMaliMagic != 0x6D616C69) {
                            logg.logError("mali_magic does not match expected value");
                            handleException();
                        }
                    }
                    break;
                }

                case PACKET_HARDWARE_COUNTER_DIRECTORY: {
                    if (header.mImplSpec == 0) {
                        constexpr size_t buffSize = sizeof(gSessionData.mSharedData->mMaliMidgardCounters);
                        if (header.mDataLength > buffSize) {
                            logg.logError("Unable to read Hardware Counter Directory Packet because it's %" PRIu32 " bytes but no more than %zu bytes was expected", header.mDataLength, buffSize);
                            handleException();
                        }

                        char * const buf = gSessionData.mSharedData->mMaliMidgardCounters;
                        if (!lib::readAll(uds, buf, header.mDataLength)) {
                            logg.logError("Unable to read Hardware Counter Directory Packet");
                            handleException();
                        }
                        gSessionData.mSharedData->mMaliMidgardCountersSize = header.mDataLength;
                        goto allDone;
                    }
                }
                // fall through

                /* no break */
                case 0x0400:
                case 0x0402:
                case 0x0408: {
                    // Ignore
                    if (!lib::skipAll(uds, header.mDataLength)) {
                        logg.logError("Unable to skip packet body");
                        handleException();
                    }

                    break;
                }

                default:
                    // Unrecognized packet, give up
                    goto allDone;
                }
            }
            allDone:

            close(uds);
        }
    }

    char * const buf = gSessionData.mSharedData->mMaliMidgardCounters;
    const size_t size = gSessionData.mSharedData->mMaliMidgardCountersSize;
    CounterData cd;
    cd.mType = CounterData::PERF;
    for (int i = 0; i + sizeof(HardwareCounter) < size;) {
        const HardwareCounter *counter = reinterpret_cast<const HardwareCounter *>(buf + i);
        char *name;
        if (asprintf(&name, "ARM_Mali-%s", counter->mCounterName) <= 0) {
            logg.logError("asprintf failed");
            handleException();
        }
        cd.mIndex = counter->mCounterIndex;
        const_cast<MidgardDriver *>(this)->setCounters(new MidgardCounter(getCounters(), name, &cd));
        ::free(name);
        i += sizeof(*counter) + counter->mCounterNameLen;
    }

    // Should a more sophisticated check be used?
    if (size > 0) {
        cd.mType = CounterData::WINDUMP;
        const_cast<MidgardDriver *>(this)->setCounters(
                new MidgardCounter(getCounters(), "ARM_Mali-Midgard_Filmstrip2_cnt0", &cd));

        cd.mType = CounterData::ACTIVITY;
        cd.mCores = 1;
        const_cast<MidgardDriver *>(this)->setCounters(
                new MidgardCounter(getCounters(), "ARM_Mali-Midgard_fragment", &cd));
        const_cast<MidgardDriver *>(this)->setCounters(
                new MidgardCounter(getCounters(), "ARM_Mali-Midgard_vertex", &cd));
        const_cast<MidgardDriver *>(this)->setCounters(
                new MidgardCounter(getCounters(), "ARM_Mali-Midgard_opencl", &cd));
    }
}

bool MidgardDriver::start(const int uds)
{
    uint64_t enabled[8] = { 0 };
    size_t bufPos = 0;
    char buf[ARRAY_LENGTH(enabled) * sizeof(GPUPerfPeriod) + sizeof(GLESWindump)];

    // [SDDAP-7729] insert some small pause to allow the remote side to correctly start up otherwise counters are
    // not always received
    usleep(10000);

    for (MidgardCounter *counter = static_cast<MidgardCounter *>(getCounters()); counter != NULL;
            counter = static_cast<MidgardCounter *>(counter->getNext())) {
        if (!counter->isEnabled() || counter->getType() != CounterData::PERF) {
            continue;
        }

        int i = counter->getIndex() / 64;
        if (i >= ARRAY_LENGTH(enabled)) {
            logg.logError("enabled is too small");
            handleException();
        }
        enabled[i] |= uint64_t(1) << (counter->getIndex() & 63);
    }

    for (int i = 0; i < ARRAY_LENGTH(enabled); ++i) {
        if (enabled[i] == 0) {
            continue;
        }

        GPUPerfPeriod m;
        // MALI_GPUPERF_PERIOD
        m.mDeclId = 0;
        m.mMicroseconds = gSessionData.mSampleRate > 0 ? 1000000 / gSessionData.mSampleRate : 100000;
        m.mStartIndex = 64 * i;
        m.mEnableMap = enabled[i];
        memcpy(buf + bufPos, &m, sizeof(m));
        bufPos += sizeof(m);
    }

    bool foundWindumpCounter = false;
    for (MidgardCounter *counter = static_cast<MidgardCounter *>(getCounters()); counter != NULL;
            counter = static_cast<MidgardCounter *>(counter->getNext())) {
        if (!counter->isEnabled() || counter->getType() != CounterData::WINDUMP) {
            continue;
        }

        if (foundWindumpCounter) {
            logg.logError("Only one Mali Midgard filmstrip counter can be enabled at a time");
            handleException();
        }
        foundWindumpCounter = true;

        // MALI_GLES_WINDUMP
        GLESWindump m;
        m.mDeclId = 1;
        m.mSkipframes = counter->getEvent() & 0xff;
        m.mMinWidth = (counter->getEvent() & 0xfff00000) >> 20;
        m.mMinHeight = (counter->getEvent() & 0xfff00) >> 8;
        memcpy(buf + bufPos, &m, sizeof(m));
        bufPos += sizeof(m);
    }

    if (bufPos > sizeof(buf)) {
        logg.logError("Buffer overflow");
        handleException();
    }

    if (!lib::writeAll(uds, buf, bufPos)) {
        logg.logError("Unable to enable Midgard counters");
        handleException();
    }

    return true;
}

bool MidgardDriver::claimCounter(Counter &counter) const
{
    // do not claim if another driver already has
    if (counter.getDriver() != NULL) {
        return false;
    }

    query();
    return super::claimCounter(counter);
}

void MidgardDriver::resetCounters()
{
    query();
    super::resetCounters();
}

void MidgardDriver::setupCounter(Counter &counter)
{
    MidgardCounter * const midgardCounter = static_cast<MidgardCounter *>(findCounter(counter));
    if (midgardCounter == NULL) {
        counter.setEnabled(false);
        return;
    }
    midgardCounter->setEnabled(true);
    counter.setKey(midgardCounter->getKey());
    if (counter.getEvent() != -1) {
        midgardCounter->setEvent(counter.getEvent());
    }
    if (midgardCounter->getType() == CounterData::ACTIVITY && midgardCounter->getCores() > 0) {
        counter.setCores(midgardCounter->getCores());
    }
}
