/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#ifndef MALI_USERSPACE_MALIHWCNTRTASK_H_
#define MALI_USERSPACE_MALIHWCNTRTASK_H_

#include "Child.h"
#include "IBuffer.h"
#include "IMaliHwCntrReader.h"
#include "MaliDevice.h"

#include <functional>

namespace mali_userspace {

    class MaliHwCntrTask {
    public:
        MaliHwCntrTask(std::function<void()> endSession,
                       std::function<bool()> isSessionActive,
                       std::function<std::int64_t()> getMonotonicStarted,
                       std::unique_ptr<IBuffer> && buffer,
                       IMaliDeviceCounterDumpCallback & callback,
                       IMaliHwCntrReader & reader);
        void execute(int sampleRate, bool isOneShot);
        bool isDone();
        void write(ISender * sender);

    private:
        std::unique_ptr<IBuffer> mBuffer;
        std::function<std::int64_t()> mGetMonotonicStarted;
        IMaliDeviceCounterDumpCallback & mCallback;
        std::function<void()> endSession;
        std::function<bool()> isSessionActive;
        IMaliHwCntrReader & mReader;

        // Intentionally unimplemented
        MaliHwCntrTask(const MaliHwCntrTask &) = delete;
        MaliHwCntrTask & operator=(const MaliHwCntrTask &) = delete;
        MaliHwCntrTask(MaliHwCntrTask &&) = delete;
        MaliHwCntrTask & operator=(MaliHwCntrTask &&) = delete;
    };
}

#endif /* MALI_USERSPACE_MALIHWCNTRTASK_H_ */
