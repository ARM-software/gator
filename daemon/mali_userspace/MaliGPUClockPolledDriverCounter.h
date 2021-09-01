/* Copyright (C) 2019-2021 by Arm Limited. All rights reserved. */

#ifndef MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVERCOUNTER_H_
#define MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVERCOUNTER_H_

#include "DriverCounter.h"

#include <cstdint>

namespace mali_userspace {
    class MaliGPUClockPolledDriverCounter : public DriverCounter {
    public:
        MaliGPUClockPolledDriverCounter(DriverCounter * next, const char * const name, uint64_t & value)
            : DriverCounter(next, name), mValue(value)
        {
        }
        ~MaliGPUClockPolledDriverCounter() override = default;

        // Intentionally unimplemented
        MaliGPUClockPolledDriverCounter(const MaliGPUClockPolledDriverCounter &) = delete;
        MaliGPUClockPolledDriverCounter & operator=(const MaliGPUClockPolledDriverCounter &) = delete;
        MaliGPUClockPolledDriverCounter(MaliGPUClockPolledDriverCounter &&) = delete;
        MaliGPUClockPolledDriverCounter & operator=(MaliGPUClockPolledDriverCounter &&) = delete;

        int64_t read() override { return mValue; }

    private:
        uint64_t & mValue;
    };

}

#endif /* MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVERCOUNTER_H_ */
