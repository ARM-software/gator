/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define __STDC_FORMAT_MACROS

#include "mali_userspace/MaliHwCntrSource.h"
#include "mali_userspace/MaliHwCntrDriver.h"
#include "mali_userspace/MaliHwCntrReader.h"
#include "mali_userspace/IMaliHwCntrReader.h"

#include <inttypes.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <algorithm>
#include "Buffer.h"

#include "Child.h"
#include "DriverSource.h"
#include "Logging.h"
#include "PrimarySourceProvider.h"
#include "Protocol.h"
#include "SessionData.h"

namespace mali_userspace
{
    MaliHwCntrSource::MaliHwCntrSource(Child & child, sem_t *senderSem, std::function<std::int64_t()> getMonotonicStarted, MaliHwCntrDriver & driver)
            : Source(child),
              mGetMonotonicStarted(getMonotonicStarted),
              mDriver(driver),
              tasks()
    {
        createTasks(senderSem);
    }

    MaliHwCntrSource::~MaliHwCntrSource()
    {
    }

    void MaliHwCntrSource::createTasks( sem_t* mSenderSem)
    {
        auto funChildEndSession = [&] {mChild.endSession();};
        auto funIsSessionActive =  [&] () -> bool {return gSessionData.mSessionIsActive;};

        for (auto& reader : mDriver.getReaders()) {
            int32_t deviceNumber = reader.first;
            std::unique_ptr<Buffer> taskBuffer(new Buffer(deviceNumber, FrameType::BLOCK_COUNTER, gSessionData.mTotalBufferSize * 1024 * 1024, mSenderSem));
            std::unique_ptr<MaliHwCntrTask> task(new MaliHwCntrTask(funChildEndSession, funIsSessionActive, mGetMonotonicStarted, std::move(taskBuffer), *this, *reader.second));
            tasks.push_back(std::move(task));
        }
    }

    bool MaliHwCntrSource::prepare()
    {
        return mDriver.start();
    }

    void MaliHwCntrSource::run()
    {
        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-malihwc"), 0, 0, 0);
        std::vector<std::thread> threadsCreated;
        for(auto const& task : tasks) {
            threadsCreated.push_back(std::thread(&MaliHwCntrTask::execute, task.get(),gSessionData.mSampleRate, gSessionData.mOneShot));
        }
        std::for_each(threadsCreated.begin(),threadsCreated.end(), std::mem_fn(&std::thread::join));
    }

    void MaliHwCntrSource::interrupt()
    {
        for(auto& reader :  mDriver.getReaders()) {
           reader.second->interrupt();
        }
    }

    bool MaliHwCntrSource::isDone()
    {
        for (auto& task : tasks) {
            if (!task->isDone()) {
                return false;
            }
        }
        return true;
    }

    void MaliHwCntrSource::write(ISender *sender)
    {
        for (auto& task : tasks) {
            if (!task->isDone()) {
                task->write(sender);
            }
        }
    }

    void MaliHwCntrSource::nextCounterValue(uint32_t nameBlockIndex, uint32_t counterIndex, uint64_t delta, uint32_t gpuId, IBuffer& buffer)
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
