/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERSOURCE_H_
#define NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERSOURCE_H_

#include <semaphore.h>
#include <functional>

#include "mali_userspace/MaliDevice.h"

#include "ClassBoilerPlate.h"
#include "IBuffer.h"
#include "Source.h"
#include <thread>
#include <vector>
#include <memory>

#include "MaliHwCntrTask.h"
class PrimarySourceProvider;

namespace mali_userspace
{
    class MaliHwCntrDriver;
    class MaliHwCntrSource : public Source, private virtual IMaliDeviceCounterDumpCallback
    {
    public:
        MaliHwCntrSource(Child & child, sem_t *senderSem, std::function<std::int64_t()> getMonotonicStarted, MaliHwCntrDriver & driver);
        ~MaliHwCntrSource();

        virtual bool prepare() override;
        virtual void run() override;
        virtual void interrupt() override;
        virtual bool isDone() override;
        virtual void write(ISender * sender) override;

    private:

        std::function<std::int64_t()> mGetMonotonicStarted;
        MaliHwCntrDriver & mDriver;
        std::vector<std::unique_ptr<MaliHwCntrTask>> tasks;

        void createTasks( sem_t* mSenderSem);

        // Intentionally unimplemented
        CLASS_DELETE_COPY_MOVE(MaliHwCntrSource);

        virtual void nextCounterValue(uint32_t nameBlockIndex, uint32_t counterIndex, uint64_t delta, uint32_t gpuId, IBuffer& bufferData) override;
        virtual bool isCounterActive(uint32_t nameBlockIndex, uint32_t counterIndex, uint32_t gpuId) const override;
    };
}

#endif // NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERSOURCE_H_
