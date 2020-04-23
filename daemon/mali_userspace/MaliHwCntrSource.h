/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERSOURCE_H_
#define NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERSOURCE_H_

#include "IBuffer.h"
#include "MaliHwCntrTask.h"
#include "Source.h"
#include "mali_userspace/MaliDevice.h"

#include <functional>
#include <memory>
#include <semaphore.h>
#include <thread>
#include <vector>
class PrimarySourceProvider;

namespace mali_userspace {
    class MaliHwCntrDriver;
    class MaliHwCntrSource : public Source, private virtual IMaliDeviceCounterDumpCallback {
    public:
        MaliHwCntrSource(Child & child,
                         sem_t * senderSem,
                         std::function<std::int64_t()> getMonotonicStarted,
                         MaliHwCntrDriver & driver);
        ~MaliHwCntrSource();

        virtual bool prepare() override;
        virtual void run() override;
        virtual void interrupt() override;
        virtual bool isDone() override;
        virtual void write(ISender * sender) override;

    private:
        MaliHwCntrDriver & mDriver;
        std::function<std::int64_t()> mGetMonotonicStarted;
        std::map<unsigned, std::unique_ptr<MaliHwCntrReader>> mReaders;
        std::vector<std::unique_ptr<MaliHwCntrTask>> tasks;

        void createTasks(sem_t * mSenderSem);

        // Intentionally unimplemented
        MaliHwCntrSource(const MaliHwCntrSource &) = delete;
        MaliHwCntrSource & operator=(const MaliHwCntrSource &) = delete;
        MaliHwCntrSource(MaliHwCntrSource &&) = delete;
        MaliHwCntrSource & operator=(MaliHwCntrSource &&) = delete;

        virtual void nextCounterValue(uint32_t nameBlockIndex,
                                      uint32_t counterIndex,
                                      uint64_t delta,
                                      uint32_t gpuId,
                                      IBuffer & bufferData) override;
        virtual bool isCounterActive(uint32_t nameBlockIndex, uint32_t counterIndex, uint32_t gpuId) const override;
    };
}

#endif // NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERSOURCE_H_
