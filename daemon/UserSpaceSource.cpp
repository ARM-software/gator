/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define __STDC_FORMAT_MACROS

#include "UserSpaceSource.h"

#include <inttypes.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "BufferUtils.h"
#include "Child.h"
#include "Drivers.h"
#include "DriverSource.h"
#include "Logging.h"
#include "PrimarySourceProvider.h"
#include "PolledDriver.h"
#include "SessionData.h"

UserSpaceSource::UserSpaceSource(Child & child, sem_t *senderSem, std::function<std::int64_t()> getMonotonicStarted, lib::Span<PolledDriver * const> drivers)
        : Source(child),
          mBuffer(0, FrameType::BLOCK_COUNTER, gSessionData.mTotalBufferSize * 1024 * 1024, senderSem),
          mGetMonotonicStarted(getMonotonicStarted),
          mDrivers(drivers)
{
}

UserSpaceSource::~UserSpaceSource()
{
}

bool UserSpaceSource::shouldStart(lib::Span<const PolledDriver * const> drivers)
{
    for (const PolledDriver * usDriver : drivers) {
        if (usDriver->countersEnabled()) {
            return true;
        }
    }
    return false;
}

bool UserSpaceSource::prepare()
{
    return true;
}

void UserSpaceSource::run()
{
    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-counters"), 0, 0, 0);

    std::vector<PolledDriver *> allUserspaceDrivers;

    for (PolledDriver * usDriver : mDrivers) {
        if (usDriver->countersEnabled()) {
            usDriver->start();
            allUserspaceDrivers.emplace_back(usDriver);
        }
    }

    int64_t monotonicStarted = 0;
    while (monotonicStarted <= 0 && gSessionData.mSessionIsActive) {
        usleep(1);
        monotonicStarted = mGetMonotonicStarted();
    }

    uint64_t nextTime = 0;
    while (gSessionData.mSessionIsActive) {
        const uint64_t currTime = getTime() - monotonicStarted;
        // Sample ten times a second ignoring gSessionData.mSampleRate
        nextTime += NS_PER_S / 10; //gSessionData.mSampleRate;
        if (nextTime < currTime) {
            logg.logMessage("Too slow, currTime: %" PRIi64 " nextTime: %" PRIi64, currTime, nextTime);
            nextTime = currTime;
        }

        if (mBuffer.eventHeader(currTime)) {
            for (PolledDriver * usDriver : allUserspaceDrivers) {
                usDriver->read(&mBuffer);
            }
            // Only check after writing all counters so that time and corresponding counters appear in the same frame
            mBuffer.check(currTime);
        }

        if (gSessionData.mOneShot && gSessionData.mSessionIsActive && (mBuffer.bytesAvailable() <= 0)) {
            logg.logMessage("One shot (counters)");
            mChild.endSession();
        }

        usleep((nextTime - currTime) / NS_PER_US);
    }

    mBuffer.setDone();
}

void UserSpaceSource::interrupt()
{
    // Do nothing
}

bool UserSpaceSource::isDone()
{
    return mBuffer.isDone();
}

void UserSpaceSource::write(ISender *sender)
{
    if (!mBuffer.isDone()) {
        mBuffer.write(sender);
    }
}
