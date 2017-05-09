/**
 * Copyright (C) ARM Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define __STDC_FORMAT_MACROS

#include "mali_userspace/MaliHwCntrSource.h"
#include "mali_userspace/MaliHwCntrDriver.h"
#include "mali_userspace/MaliHwCntrReader.h"

#include <inttypes.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "Child.h"
#include "DriverSource.h"
#include "Logging.h"
#include "PrimarySourceProvider.h"
#include "SessionData.h"

namespace mali_userspace
{

    MaliHwCntrSource::MaliHwCntrSource(Child & child, sem_t *senderSem)
            : Source(child),
              mBuffer(0, FRAME_BLOCK_COUNTER, gSessionData.mTotalBufferSize * 1024 * 1024, senderSem)
    {
    }

    MaliHwCntrSource::~MaliHwCntrSource()
    {
    }

    bool MaliHwCntrSource::prepare()
    {
        return gSessionData.mMaliHwCntrs.start();
    }

    void MaliHwCntrSource::run()
    {
        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-malihwc"), 0, 0, 0);

        MaliHwCntrReader * const reader = gSessionData.mMaliHwCntrs.getReader();

        if (reader != NULL) {
            int64_t monotonicStarted = 0;
            while (monotonicStarted <= 0 && gSessionData.mSessionIsActive) {
                usleep(1);
                monotonicStarted = gSessionData.mPrimarySource->getMonotonicStarted();
            }

            bool terminated = false;

            // set sample interval, if sample rate == 0, then sample at 100Hz as currently the job dumping based sampling does not work... (driver issue?)
            const uint32_t sampleIntervalNs = (gSessionData.mSampleRate > 0
                        ? (gSessionData.mSampleRate < 1000000000 ? (1000000000u / gSessionData.mSampleRate) : 1u)
                        : 10000000u);

            if (!reader->startPeriodicSampling(sampleIntervalNs)) {
                logg.logError("Could not enable periodic sampling");
                terminated = true;
            }

            // create the list of enabled counters
            const MaliDeviceCounterList countersList (reader->getDevice().createCounterList(*this));

            while (gSessionData.mSessionIsActive && !terminated) {
                SampleBuffer sampleBuffer;
                MaliHwCntrReader::WaitStatus waitStatus = reader->waitForBuffer(sampleBuffer, 10000);

                switch (waitStatus) {
                case MaliHwCntrReader::WAIT_STATUS_SUCCESS: {
                    if (sampleBuffer.isValid()) {
                        const uint64_t sampleTime = sampleBuffer.getTimestamp() - monotonicStarted;

                        if (mBuffer.eventHeader(sampleTime)) {
                            reader->getDevice().dumpAllCounters(reader->getHardwareVersion(), reader->getMmuL2BlockCount(),
                                                                countersList,
                                                                reinterpret_cast<const uint32_t *>(sampleBuffer.getData()),
                                                                sampleBuffer.getSize() / sizeof(uint32_t),
                                                                *this);
                            mBuffer.check(sampleTime);
                        }
                    }
                    break;
                }
                case MaliHwCntrReader::WAIT_STATUS_TERMINATED: {
                    logg.logMessage("Stopped capturing HW counters");
                    terminated = true;
                    break;
                }
                case MaliHwCntrReader::WAIT_STATUS_ERROR:
                default: {
                    logg.logError("Error - Stopped capturing HW counters");
                    break;
                }
                }

                if (gSessionData.mOneShot && gSessionData.mSessionIsActive && (mBuffer.bytesAvailable() <= 0)) {
                    logg.logMessage("One shot (malihwc)");
                    mChild.endSession();
                }
            }

            if (!reader->startPeriodicSampling(0)) {
                logg.logError("Could not disable periodic sampling");
            }
        }

        mBuffer.setDone();
    }

    void MaliHwCntrSource::interrupt()
    {
        // Do nothing
        MaliHwCntrReader * const reader = gSessionData.mMaliHwCntrs.getReader();

        if (reader != NULL) {
            reader->interrupt();
        }
    }

    bool MaliHwCntrSource::isDone()
    {
        return mBuffer.isDone();
    }

    void MaliHwCntrSource::write(Sender *sender)
    {
        if (!mBuffer.isDone()) {
            mBuffer.write(sender);
        }
    }

    void MaliHwCntrSource::nextCounterValue(uint32_t nameBlockIndex, uint32_t counterIndex, uint64_t delta)
    {
        const int key = gSessionData.mMaliHwCntrs.getCounterKey(nameBlockIndex, counterIndex);

        if (key != 0) {
            mBuffer.event64(key, delta);
        }
    }

    bool MaliHwCntrSource::isCounterActive(uint32_t nameBlockIndex, uint32_t counterIndex) const
    {
        const int key = gSessionData.mMaliHwCntrs.getCounterKey(nameBlockIndex, counterIndex);

        return (key != 0);
    }
}
