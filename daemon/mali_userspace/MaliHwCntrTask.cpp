/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#include "MaliHwCntrTask.h"

#include "Logging.h"
#include "SessionData.h"
#include "lib/Syscall.h"

#include <unistd.h>
#include <utility>

namespace mali_userspace {
    MaliHwCntrTask::MaliHwCntrTask(std::function<void()> endSession_,
                                   std::function<bool()> isSessionActive_,
                                   std::function<std::int64_t()> getMonotonicStarted,
                                   std::unique_ptr<IBuffer> && buffer_,
                                   IMaliDeviceCounterDumpCallback & callback_,
                                   IMaliHwCntrReader & reader)
        : mBuffer(std::move(buffer_)),
          mGetMonotonicStarted(std::move(getMonotonicStarted)),
          mCallback(callback_),
          endSession(std::move(endSession_)),
          isSessionActive(std::move(isSessionActive_)),
          mReader(reader)
    {
    }

    void MaliHwCntrTask::execute(int sampleRate, bool isOneShot)
    {
        int64_t monotonicStarted = 0;
        //should the thread be interrupted in this while??
        while (monotonicStarted <= 0 && isSessionActive()) {
            usleep(1);
            monotonicStarted = mGetMonotonicStarted();
        }

        bool terminated = false;
        // set sample interval, if sample rate == 0, then sample at 100Hz as currently the job dumping based sampling does not work... (driver issue?)
        const uint32_t sampleIntervalNs =
            (sampleRate > 0 ? (sampleRate < 1000000000 ? (1000000000U / sampleRate) : 1U) : 10000000U);

        if (!mReader.startPeriodicSampling(sampleIntervalNs)) {
            logg.logError("Could not enable periodic sampling");
            terminated = true;
        }
        // create the list of enabled counters
        const MaliDeviceCounterList countersList(mReader.getDevice().createCounterList(mCallback));
        while (isSessionActive() && !terminated) {
            SampleBuffer waitStatus = mReader.waitForBuffer(10000);

            switch (waitStatus.status) {
                case WAIT_STATUS_SUCCESS: {
                    if (waitStatus.data) {
                        const uint64_t sampleTime = waitStatus.timestamp - monotonicStarted;
                        IBlockCounterFrameBuilder & builder = *mBuffer;
                        if (builder.eventHeader(sampleTime)) {
                            mReader.getDevice().dumpAllCounters(
                                mReader.getHardwareVersion(),
                                countersList,
                                reinterpret_cast<const uint32_t *>(waitStatus.data.get()),
                                waitStatus.size / sizeof(uint32_t),
                                builder,
                                mCallback);
                            builder.check(sampleTime);
                        }
                    }
                    break;
                }
                case WAIT_STATUS_TERMINATED: {
                    logg.logMessage("Stopped capturing HW counters");
                    terminated = true;
                    break;
                }
                case WAIT_STATUS_ERROR:
                default: {
                    logg.logError("Error - Stopped capturing HW counters");
                    break;
                }
            }
            if (isOneShot && isSessionActive() && (mBuffer->bytesAvailable() <= 0)) {
                logg.logMessage("One shot (malihwc)");
                endSession();
            }
        }

        if (!mReader.startPeriodicSampling(0)) {
            logg.logError("Could not disable periodic sampling");
        }
        mBuffer->setDone();
    }

    bool MaliHwCntrTask::isDone() { return mBuffer->isDone(); }

    void MaliHwCntrTask::write(ISender & sender)
    {
        if (!mBuffer->isDone()) {
            mBuffer->write(sender);
        }
    }
}
