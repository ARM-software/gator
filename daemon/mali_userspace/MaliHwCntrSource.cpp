/* Copyright (C) 2010-2023 by Arm Limited. All rights reserved. */

#define __STDC_FORMAT_MACROS
#define BUFFER_USE_SESSION_DATA

#include "mali_userspace/MaliHwCntrSource.h"

#include "BlockCounterFrameBuilder.h"
#include "Buffer.h"
#include "MaliHwCntrTask.h"
#include "SessionData.h"
#include "Source.h"
#include "mali_userspace/MaliDevice.h"
#include "mali_userspace/MaliHwCntrDriver.h"
#include "mali_userspace/MaliHwCntrTask.h"

#include <algorithm>
#include <cinttypes>
#include <functional>
#include <map>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <semaphore.h>
#include <sys/prctl.h>
#include <unistd.h>

namespace mali_userspace {
    class MaliHwCntrSource : public Source, public virtual IMaliDeviceCounterDumpCallback {
    public:
        MaliHwCntrSource(sem_t & senderSem, MaliHwCntrDriver & driver) : mDriver(driver) { createTasks(senderSem); }

        void createTasks(sem_t & mSenderSem)
        {
            for (const auto & pair : mDriver.getDevices()) {
                const auto deviceNumber = static_cast<std::int32_t>(pair.first);
                const MaliDevice & device = *pair.second;

                // NOLINTNEXTLINE(readability-magic-numbers)
                std::unique_ptr<Buffer> taskBuffer(new Buffer(gSessionData.mTotalBufferSize * 1024 * 1024, mSenderSem));

                std::unique_ptr<BlockCounterFrameBuilder> frameBuilder(
                    new BlockCounterFrameBuilder(*taskBuffer, gSessionData.mLiveRate));
                std::unique_ptr<MaliHwCntrTask> task(new MaliHwCntrTask(std::move(taskBuffer),
                                                                        std::move(frameBuilder),
                                                                        deviceNumber,
                                                                        *this,
                                                                        device,
                                                                        device.getConstantValues()));
                tasks.push_back(std::move(task));
            }
        }

        bool prepare() { return mDriver.start(); }

        void run(std::uint64_t monotonicStarted, std::function<void()> endSession) override
        {
            prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-malihwc"), 0, 0, 0);
            std::vector<std::thread> threadsCreated;
            const int sampleRate = gSessionData.mSampleRate;
            const bool isOneShot = gSessionData.mOneShot;
            for (auto const & task : tasks) {
                MaliHwCntrTask * const taskPtr = task.get();
                threadsCreated.emplace_back([=]() -> void {
                    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-malihtsk"), 0, 0, 0);
                    taskPtr->execute(sampleRate, isOneShot, monotonicStarted, endSession);
                });
            }
            std::for_each(threadsCreated.begin(), threadsCreated.end(), std::mem_fn(&std::thread::join));
        }

        void interrupt() override
        {
            for (auto & task : tasks) {
                task->interrupt();
            }
        }

        bool write(ISender & sender) override
        {
            bool done = true;
            for (auto & task : tasks) {
                // bitwise &, no short-circut;
                done &= task->write(sender);
            }
            return done;
        }

        void nextCounterValue(uint32_t nameBlockIndex,
                              uint32_t counterIndex,
                              uint64_t delta,
                              uint32_t gpuId,
                              IBlockCounterFrameBuilder & buffer) override
        {
            const int key = mDriver.getCounterKey(nameBlockIndex, counterIndex, gpuId);
            if (key != 0) {
                buffer.event64(key, delta);
            }
        }

        [[nodiscard]] bool isCounterActive(uint32_t nameBlockIndex,
                                           uint32_t counterIndex,
                                           uint32_t gpuId) const override
        {
            const int key = mDriver.getCounterKey(nameBlockIndex, counterIndex, gpuId);
            return (key != 0);
        }

    private:
        MaliHwCntrDriver & mDriver;
        std::vector<std::unique_ptr<MaliHwCntrTask>> tasks {};
    };

    std::shared_ptr<Source> createMaliHwCntrSource(sem_t & senderSem, MaliHwCntrDriver & driver)
    {
        auto source = std::make_shared<MaliHwCntrSource>(senderSem, driver);
        if (!source->prepare()) {
            return {};
        }
        return source;
    }
}
