/* Copyright (c) 2019 by Arm Limited. All rights reserved. */

#ifndef MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVERCOUNTER_H_
#define MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVERCOUNTER_H_

#include "ClassBoilerPlate.h"

namespace mali_userspace {
    class MaliGPUClockPolledDriverCounter : public DriverCounter
    {
    public:
        MaliGPUClockPolledDriverCounter(DriverCounter *next, const char * const name, uint64_t & value)
                : DriverCounter(next, name),
                  mValue(value)
        {
        }
        ~MaliGPUClockPolledDriverCounter()
        {
        }

        int64_t read()
        {
            return mValue;
        }

    private:
        uint64_t & mValue;

        // Intentionally unimplemented
        CLASS_DELETE_COPY_MOVE(MaliGPUClockPolledDriverCounter);
    };

}

#endif /* MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVERCOUNTER_H_ */
