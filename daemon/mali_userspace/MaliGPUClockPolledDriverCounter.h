/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#ifndef MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVERCOUNTER_H_
#define MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVERCOUNTER_H_

namespace mali_userspace {
    class MaliGPUClockPolledDriverCounter : public DriverCounter {
    public:
        MaliGPUClockPolledDriverCounter(DriverCounter * next, const char * const name, uint64_t & value)
            : DriverCounter(next, name), mValue(value)
        {
        }
        ~MaliGPUClockPolledDriverCounter() {}

        int64_t read() { return mValue; }

    private:
        uint64_t & mValue;

        // Intentionally unimplemented
        MaliGPUClockPolledDriverCounter(const MaliGPUClockPolledDriverCounter &) = delete;
        MaliGPUClockPolledDriverCounter & operator=(const MaliGPUClockPolledDriverCounter &) = delete;
        MaliGPUClockPolledDriverCounter(MaliGPUClockPolledDriverCounter &&) = delete;
        MaliGPUClockPolledDriverCounter & operator=(MaliGPUClockPolledDriverCounter &&) = delete;
    };

}

#endif /* MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVERCOUNTER_H_ */
