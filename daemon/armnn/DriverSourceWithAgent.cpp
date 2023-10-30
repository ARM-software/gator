/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#include "armnn/DriverSourceWithAgent.h"

#include "Logging.h"
#include "lib/Assert.h"

namespace armnn {

    DriverSourceWithAgent::ActiveSessionSetUnset::ActiveSessionSetUnset(DriverSourceWithAgent & driverSource,
                                                                        ICounterConsumer & counterConsumer)
        : mDriverSource(driverSource)
    {
        runtime_assert(mDriverSource.mSessionCounterConsumer == nullptr, "DriverSourceWithAgent in unexpected state");

        mDriverSource.mSessionCounterConsumer = &counterConsumer;
    }

    DriverSourceWithAgent::ActiveSessionSetUnset::~ActiveSessionSetUnset() noexcept
    {
        mDriverSource.mSessionCounterConsumer = nullptr;
    }

    DriverSourceWithAgent::DriverSourceWithAgent(ICaptureStartStopHandler & startStopHandler)
        : mArmnnController {startStopHandler}
    {
    }

    bool DriverSourceWithAgent::consumeCounterValue(std::uint64_t timestamp,
                                                    ApcCounterKeyAndCoreNumber keyAndCore,
                                                    std::uint32_t counterValue)
    {
        const std::unique_lock<std::mutex> guard {mSessionMutex};

        if (mSessionCounterConsumer == nullptr) {
            return true;
        }

        // send the data
        if (mSessionCounterConsumer->consumeCounterValue(timestamp, keyAndCore, counterValue)) {
            return true;
        }

        // after sending, notify, so that the run function can check for buffer full
        mBufferFull = true;
        mSessionNotify.notify_one();

        return false;
    }

    bool DriverSourceWithAgent::consumePacket(std::uint32_t sessionId, lib::Span<const std::uint8_t> data)
    {
        const std::unique_lock<std::mutex> guard {mSessionMutex};

        if (mSessionCounterConsumer == nullptr) {
            return true;
        }

        // send the data
        if (mSessionCounterConsumer->consumePacket(sessionId, data)) {
            return true;
        }

        // after sending, notify, so that the run function can check for buffer full
        mBufferFull = true;
        mSessionNotify.notify_one();

        return false;
    }

    void DriverSourceWithAgent::run(ICounterConsumer & counterConsumer,
                                    bool isOneShot,
                                    std::function<void()> endSession,
                                    std::function<unsigned int()> /*getBufferBytesAvailable*/)
    {
        std::unique_lock<std::mutex> guard {mSessionMutex};

        // set the mSessionCounterConsumer to the counterConsumer for the duration of the session
        ActiveSessionSetUnset const activeSessionSet {*this, counterConsumer};

        // start the session
        mArmnnController.startCapture();

        // wait for something to happen
        while ((!mInterrupted) && (!mBufferFull)) {
            mSessionNotify.wait(guard);
        }

        // stop the session
        mArmnnController.stopCapture();

        // was one-shot buffer full?
        if (mBufferFull && isOneShot) {
            LOG_ERROR("One shot (Arm NN)");
            endSession();
        }
    }

    void DriverSourceWithAgent::interrupt()
    {
        {
            const std::unique_lock<std::mutex> guard {mSessionMutex};
            mInterrupted = true;
        }

        mSessionNotify.notify_one();
    }
}
