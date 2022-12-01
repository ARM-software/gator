/* Copyright (C) 2019-2022 by Arm Limited. All rights reserved. */

#ifndef MALI_USERSPACE_MALIHWCNTRTASK_H_
#define MALI_USERSPACE_MALIHWCNTRTASK_H_

#include "Child.h"
#include "MaliDevice.h"
#include "device/hwcnt/reader.hpp"

#include <array>
#include <functional>
#include <memory>
#include <system_error>

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
                       const MaliDevice & device,
                       std::map<CounterKey, int64_t> constants);

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

        void interrupt();

    private:
        std::unique_ptr<IBufferControl> mBuffer;
        std::unique_ptr<IBlockCounterFrameBuilder> mFrameBuilder;
        IMaliDeviceCounterDumpCallback & mCallback;
        const MaliDevice & mDevice;
        std::int32_t deviceNumber;
        const std::map<CounterKey, int64_t> mConstantValues;
        std::array<int, 2> interrupt_fd;

        std::error_code write_sample(const MaliDeviceCounterList & counter_list,
                                     hwcpipe::device::hwcnt::reader & reader,
                                     std::uint64_t monotonic_start);

        bool writeConstants();
    };
}

#endif /* MALI_USERSPACE_MALIHWCNTRTASK_H_ */
