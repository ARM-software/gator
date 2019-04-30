/* Copyright (c) 2019 by Arm Limited. All rights reserved. */

#ifndef MALI_USERSPACE_MALIHWCNTR_H_
#define MALI_USERSPACE_MALIHWCNTR_H_

#include "ClassBoilerPlate.h"

namespace mali_userspace
{
    class MaliHwCntr : public DriverCounter
    {
    public:

        MaliHwCntr(DriverCounter * next, const char * name, int32_t nameBlockIndex, int32_t counterIndex,
                   uint32_t gpuId)
                : DriverCounter(next, name),
                  mNameBlockIndex(nameBlockIndex),
                  mCounterIndex(counterIndex),
                  mGpuId(gpuId)
        {
        }

        inline int32_t getNameBlockIndex() const
        {
            return mNameBlockIndex;
        }

        inline int32_t getCounterIndex() const
        {
            return mCounterIndex;
        }

        inline uint32_t getGpuId() const
        {
            return mGpuId;
        }
    private:
        int32_t mNameBlockIndex;
        int32_t mCounterIndex;
        uint32_t mGpuId;

        // Intentionally undefined
        CLASS_DELETE_COPY_MOVE(MaliHwCntr)
        ;
    };
}

#endif /* MALI_USERSPACE_MALIHWCNTR_H_ */
