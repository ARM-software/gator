/* Copyright (C) 2019-2021 by Arm Limited. All rights reserved. */

#ifndef MALI_USERSPACE_MALIHWCNTRTASK_H_
#define MALI_USERSPACE_MALIHWCNTRTASK_H_

#include "Child.h"
#include "IMaliHwCntrReader.h"
#include "MaliDevice.h"

#include <functional>

class IBufferControl;
class IBlockCounterFrameBuilder;
class ISender;

namespace mali_userspace {

    class MaliHwCntrTask {
    public:
        /**
         * @param frameBuilder will not outlive buffer
         */
        MaliHwCntrTask(std::unique_ptr<IBufferControl> buffer,
                       std::unique_ptr<IBlockCounterFrameBuilder> frameBuilder,
                       std::int32_t deviceNumber,
                       IMaliDeviceCounterDumpCallback & callback,
                       IMaliHwCntrReader & reader,
                       const std::map<CounterKey, int64_t> & constants);

        // Intentionally unimplemented
        MaliHwCntrTask(const MaliHwCntrTask &) = delete;
        MaliHwCntrTask & operator=(const MaliHwCntrTask &) = delete;
        MaliHwCntrTask(MaliHwCntrTask &&) = delete;
        MaliHwCntrTask & operator=(MaliHwCntrTask &&) = delete;

        void execute(int sampleRate,
                     bool isOneShot,
                     std::uint64_t monotonicStart,
                     const std::function<void()> & endSession);
        bool write(ISender & sender);

    private:
        std::unique_ptr<IBufferControl> mBuffer;
        std::unique_ptr<IBlockCounterFrameBuilder> mFrameBuilder;
        IMaliDeviceCounterDumpCallback & mCallback;
        IMaliHwCntrReader & mReader;
        std::int32_t deviceNumber;
        const std::map<CounterKey, int64_t> mConstantValues;

        bool writeConstants();
    };
}

#endif /* MALI_USERSPACE_MALIHWCNTRTASK_H_ */
