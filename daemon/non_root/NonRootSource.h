/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_NONROOTSOURCE_H
#define INCLUDE_NON_ROOT_NONROOTSOURCE_H

#include "Buffer.h"
#include "Source.h"
#include "lib/TimestampSource.h"
#include "non_root/PerCoreMixedFrameBuffer.h"

#include <atomic>
#include <semaphore.h>

class ICpuInfo;

namespace non_root
{
    class NonRootDriver;

    /**
     * Non-root Capture driver
     */
    class NonRootSource : public Source
    {
    public:

        NonRootSource(NonRootDriver & driver, Child & child, sem_t & senderSem, sem_t & startProfile, const ICpuInfo & cpuInfo);

        virtual bool prepare() override;
        virtual void run() override;
        virtual void interrupt() override;
        virtual bool isDone() override;
        virtual void write(ISender * sender) override;

    private:

        PerCoreMixedFrameBuffer mSwitchBuffers;
        Buffer mGlobalCounterBuffer;
        Buffer mProcessCounterBuffer;
        Buffer mMiscBuffer;
        std::atomic<bool> interrupted;
        lib::TimestampSource timestampSource;
        NonRootDriver & driver;
        sem_t & senderSem;
        sem_t & startProfile;
        bool done;
        const ICpuInfo & cpuInfo;

        bool summary();
        unsigned long long getBootTimeTicksBase();
    };
}

#endif /* INCLUDE_NON_ROOT_NONROOTSOURCE_H */
