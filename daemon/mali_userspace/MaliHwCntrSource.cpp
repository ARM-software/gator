/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#define __STDC_FORMAT_MACROS
#define BUFFER_USE_SESSION_DATA

#include "mali_userspace/MaliHwCntrSource.h"

#include "Buffer.h"
#include "Child.h"
#include "Logging.h"
#include "PrimarySourceProvider.h"
#include "Protocol.h"
#include "SessionData.h"
#include "mali_userspace/IMaliHwCntrReader.h"
#include "mali_userspace/MaliHwCntrDriver.h"
#include "mali_userspace/MaliHwCntrReader.h"

#include <algorithm>
#include <cinttypes>
#include <sys/prctl.h>
#include <unistd.h>
#include <utility>

namespace mali_userspace {
    MaliHwCntrSource::MaliHwCntrSource(Child & child,
                                       sem_t & senderSem,
                                       std::function<std::int64_t()> getMonotonicStarted,
                                       MaliHwCntrDriver & driver)
        : Source(child), mDriver(driver), mGetMonotonicStarted(std::move(getMonotonicStarted)), mReaders(), tasks()
    {
        createTasks(senderSem);
    }

    void MaliHwCntrSource::createTasks(sem_t & mSenderSem)
    {
        auto funChildEndSession = [&] { mChild.endSession(); };
        auto funIsSessionActive = [&]() -> bool { return gSessionData.mSessionIsActive; };

        for (const auto & pair : mDriver.getDevices()) {
            const int32_t deviceNumber = pair.first;
            const MaliDevice & device = *pair.second;

            std::unique_ptr<MaliHwCntrReader> reader = MaliHwCntrReader::createReader(device);
            if (!reader) {
                logg.logError(
                    "Failed to create reader for mali GPU # %d. Please try again, and if this happens repeatedly "
                    "please try rebooting your device.",
                    deviceNumber);
                handleException();
            }
            else {
                MaliHwCntrReader & readerRef = *reader;
                mReaders[deviceNumber] = std::move(reader);
                std::unique_ptr<Buffer> taskBuffer(new Buffer(deviceNumber,
                                                              FrameType::BLOCK_COUNTER,
                                                              gSessionData.mTotalBufferSize * 1024 * 1024,
                                                              mSenderSem));
                std::unique_ptr<MaliHwCntrTask> task(new MaliHwCntrTask(funChildEndSession,
                                                                        funIsSessionActive,
                                                                        mGetMonotonicStarted,
                                                                        std::move(taskBuffer),
                                                                        *this,
                                                                        readerRef));
                tasks.push_back(std::move(task));
            }
        }
    }

    bool MaliHwCntrSource::prepare() { return mDriver.start(); }

    void MaliHwCntrSource::run()
    {
        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-malihwc"), 0, 0, 0);
        std::vector<std::thread> threadsCreated;
        const int sampleRate = gSessionData.mSampleRate;
        const bool isOneShot = gSessionData.mOneShot;
        for (auto const & task : tasks) {
            MaliHwCntrTask * const taskPtr = task.get();
            threadsCreated.emplace_back([=]() -> void {
                prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-malihtsk"), 0, 0, 0);
                taskPtr->execute(sampleRate, isOneShot);
            });
        }
        std::for_each(threadsCreated.begin(), threadsCreated.end(), std::mem_fn(&std::thread::join));
    }

    void MaliHwCntrSource::interrupt()
    {
        for (auto & reader : mReaders) {
            reader.second->interrupt();
        }
    }

    bool MaliHwCntrSource::isDone()
    {
        for (auto & task : tasks) {
            if (!task->isDone()) {
                return false;
            }
        }
        return true;
    }

    void MaliHwCntrSource::write(ISender & sender)
    {
        for (auto & task : tasks) {
            if (!task->isDone()) {
                task->write(sender);
            }
        }
    }

    void MaliHwCntrSource::nextCounterValue(uint32_t nameBlockIndex,
                                            uint32_t counterIndex,
                                            uint64_t delta,
                                            uint32_t gpuId,
                                            IBlockCounterFrameBuilder & buffer)
    {
        const int key = mDriver.getCounterKey(nameBlockIndex, counterIndex, gpuId);
        if (key != 0) {
            buffer.event64(key, delta);
        }
    }

    bool MaliHwCntrSource::isCounterActive(uint32_t nameBlockIndex, uint32_t counterIndex, uint32_t gpuId) const
    {
        const int key = mDriver.getCounterKey(nameBlockIndex, counterIndex, gpuId);
        return (key != 0);
    }
}
