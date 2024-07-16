/* Copyright (C) 2010-2024 by Arm Limited. All rights reserved. */

// Define to get format macros from inttypes.h
#define __STDC_FORMAT_MACROS
// must be before includes

// Define to adjust Buffer.h interface,
#define BUFFER_USE_SESSION_DATA
// must be before includes

#include "UserSpaceSource.h"

#include "BlockCounterFrameBuilder.h"
#include "Buffer.h"
#include "Logging.h"
#include "PolledDriver.h"
#include "SessionData.h"
#include "Source.h"
#include "Time.h"
#include "lib/Span.h"
#include "monotonic_pair.h"

#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <semaphore.h>
#include <sys/prctl.h>
#include <unistd.h>

class UserSpaceSource : public Source {
public:
    UserSpaceSource(sem_t & senderSem, lib::Span<PolledDriver * const> drivers)
        : mBuffer(gSessionData.mTotalBufferSize * 1024 * 1024, senderSem), mDrivers(drivers)
    {
    }

    void run(monotonic_pair_t monotonicStart, std::function<void()> endSession) override
    {
        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-counters"), 0, 0, 0);

        std::vector<PolledDriver *> allUserspaceDrivers;

        for (PolledDriver * usDriver : mDrivers) {
            if (usDriver->countersEnabled()) {
                usDriver->start();
                allUserspaceDrivers.emplace_back(usDriver);
            }
        }

        constexpr uint64_t tenPerSecond = 10; // Sample ten times a second ignoring gSessionData.mSampleRate
        uint64_t nextTime = 0;
        while (mSessionIsActive) {
            const uint64_t currTime = getTime() - monotonicStart.monotonic_raw;

            nextTime += NS_PER_S / tenPerSecond; //gSessionData.mSampleRate;
            if (nextTime < currTime) {
                LOG_WARNING("Too slow, currTime: %" PRIi64 " nextTime: %" PRIi64, currTime, nextTime);
                nextTime = currTime;
            }

            BlockCounterFrameBuilder builder {mBuffer, gSessionData.mLiveRate};
            if (builder.eventHeader(currTime)) {
                for (PolledDriver * usDriver : allUserspaceDrivers) {
                    usDriver->read(builder);
                }
                // Only check after writing all counters so that time and corresponding counters appear in the same frame
                builder.check(currTime);
            }

            if (gSessionData.mOneShot && mSessionIsActive && (mBuffer.bytesAvailable() <= 0)) {
                LOG_DEBUG("One shot (counters)");
                endSession();
            }

            usleep((nextTime - currTime) / NS_PER_US);
        }

        mBuffer.setDone();
    }

    void interrupt() override { mSessionIsActive = false; }

    bool write(ISender & sender) override { return mBuffer.write(sender); }

private:
    Buffer mBuffer;
    lib::Span<PolledDriver * const> mDrivers;
    std::atomic_bool mSessionIsActive {true};
};

bool shouldStartUserSpaceSource(lib::Span<const PolledDriver * const> drivers)
{
    for (const PolledDriver * usDriver : drivers) {
        if (usDriver->countersEnabled()) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<Source> createUserSpaceSource(sem_t & senderSem, lib::Span<PolledDriver * const> drivers)
{
    return std::make_shared<UserSpaceSource>(senderSem, drivers);
}
