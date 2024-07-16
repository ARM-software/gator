/* Copyright (C) 2017-2024 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_NONROOTSOURCE_H
#define INCLUDE_NON_ROOT_NONROOTSOURCE_H

#include "Buffer.h"
#include "Source.h"
#include "lib/TimestampSource.h"
#include "non_root/PerCoreMixedFrameBuffer.h"

#include <atomic>
#include <functional>

#include <semaphore.h>

class ICpuInfo;
struct monotonic_pair_t;

namespace non_root {
    class NonRootDriver;

    /**
     * Non-root Capture driver
     */
    class NonRootSource : public PrimarySource {
    public:
        NonRootSource(NonRootDriver & driver,
                      sem_t & senderSem,
                      std::function<void()> execTargetAppCallback,
                      std::function<void()> profilingStartedCallback,
                      const ICpuInfo & cpuInfo);

        std::optional<monotonic_pair_t> sendSummary() override;
        void run(monotonic_pair_t, std::function<void()> endSession) override;
        void interrupt() override;
        bool write(ISender & sender) override;

    private:
        PerCoreMixedFrameBuffer mSwitchBuffers;
        Buffer mGlobalCounterBuffer;
        Buffer mProcessCounterBuffer;
        Buffer mMiscBuffer;
        std::atomic<bool> interrupted;
        lib::TimestampSource timestampSourceClockMonotonicRaw;
        lib::TimestampSource timestampSourceClockMonotonic;
        NonRootDriver & driver;
        std::function<void()> execTargetAppCallback;
        std::function<void()> profilingStartedCallback;
        const ICpuInfo & cpuInfo;

        unsigned long long getBootTimeTicksBase();
    };
}

#endif /* INCLUDE_NON_ROOT_NONROOTSOURCE_H */
