/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_PROCESSCOUNTER_H
#define INCLUDE_NON_ROOT_PROCESSCOUNTER_H

#include "non_root/NonRootCounter.h"

namespace non_root
{
    enum class AbsoluteProcessCounter : typename std::underlying_type<NonRootCounter>::type
    {
        DATA_SIZE = NonRootCounterValue(NonRootCounter::PROCESS_ABS_DATA_SIZE),
        NUM_THREADS = NonRootCounterValue(NonRootCounter::PROCESS_ABS_NUM_THREADS),
        RES_LIMIT = NonRootCounterValue(NonRootCounter::PROCESS_ABS_RES_LIMIT),
        RES_SIZE = NonRootCounterValue(NonRootCounter::PROCESS_ABS_RES_SIZE),
        SHARED_SIZE = NonRootCounterValue(NonRootCounter::PROCESS_ABS_SHARED_SIZE),
        TEXT_SIZE = NonRootCounterValue(NonRootCounter::PROCESS_ABS_TEXT_SIZE),
        VM_SIZE = NonRootCounterValue(NonRootCounter::PROCESS_ABS_VM_SIZE),
    };

    enum class DeltaProcessCounter : typename std::underlying_type<NonRootCounter>::type
    {
        MAJOR_FAULTS = NonRootCounterValue(NonRootCounter::PROCESS_DELTA_MAJOR_FAULTS),
        MINOR_FAULTS = NonRootCounterValue(NonRootCounter::PROCESS_DELTA_MINOR_FAULTS),
        UTIME = NonRootCounterValue(NonRootCounter::PROCESS_DELTA_UTIME),
        STIME = NonRootCounterValue(NonRootCounter::PROCESS_DELTA_STIME),
        GUEST_TIME = NonRootCounterValue(NonRootCounter::PROCESS_DELTA_GUEST_TIME)
    };
}

#endif /* INCLUDE_NON_ROOT_PROCESSCOUNTER_H */
