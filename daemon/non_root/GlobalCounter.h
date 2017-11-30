/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_GLOBALCOUNTER_H
#define INCLUDE_NON_ROOT_GLOBALCOUNTER_H

#include "non_root/NonRootCounter.h"

namespace non_root
{
    enum class AbsoluteGlobalCounter : typename std::underlying_type<NonRootCounter>::type
    {
        LOADAVG_1_MINUTE = NonRootCounterValue(NonRootCounter::GLOBAL_ABS_LOADAVG_1_MINUTE),
        LOADAVG_5_MINUTES = NonRootCounterValue(NonRootCounter::GLOBAL_ABS_LOADAVG_5_MINUTES),
        LOADAVG_15_MINUTES = NonRootCounterValue(NonRootCounter::GLOBAL_ABS_LOADAVG_15_MINUTES),
        NUM_PROCESSES_EXISTING = NonRootCounterValue(NonRootCounter::GLOBAL_ABS_NUM_PROCESSES_EXISTING),
        NUM_PROCESSES_RUNNING = NonRootCounterValue(NonRootCounter::GLOBAL_ABS_NUM_PROCESSES_RUNNING)
    };

    enum class DeltaGlobalCounter : typename std::underlying_type<NonRootCounter>::type
    {
        NUM_CONTEXT_SWITCHES = NonRootCounterValue(NonRootCounter::GLOBAL_DELTA_NUM_CONTEXT_SWITCHES),
        NUM_FORKS = NonRootCounterValue(NonRootCounter::GLOBAL_DELTA_NUM_FORKS),
        NUM_IRQ = NonRootCounterValue(NonRootCounter::GLOBAL_DELTA_NUM_IRQ),
        NUM_SOFTIRQ = NonRootCounterValue(NonRootCounter::GLOBAL_DELTA_NUM_SOFTIRQ),
        TIME_CPU_GUEST_NICE = NonRootCounterValue(NonRootCounter::GLOBAL_DELTA_TIME_CPU_GUEST_NICE),
        TIME_CPU_GUEST = NonRootCounterValue(NonRootCounter::GLOBAL_DELTA_TIME_CPU_GUEST),
        TIME_CPU_IDLE = NonRootCounterValue(NonRootCounter::GLOBAL_DELTA_TIME_CPU_IDLE),
        TIME_CPU_IOWAIT = NonRootCounterValue(NonRootCounter::GLOBAL_DELTA_TIME_CPU_IOWAIT),
        TIME_CPU_IRQ = NonRootCounterValue(NonRootCounter::GLOBAL_DELTA_TIME_CPU_IRQ),
        TIME_CPU_NICE = NonRootCounterValue(NonRootCounter::GLOBAL_DELTA_TIME_CPU_NICE),
        TIME_CPU_SOFTIRQ = NonRootCounterValue(NonRootCounter::GLOBAL_DELTA_TIME_CPU_SOFTIRQ),
        TIME_CPU_STEAL = NonRootCounterValue(NonRootCounter::GLOBAL_DELTA_TIME_CPU_STEAL),
        TIME_CPU_SYSTEM = NonRootCounterValue(NonRootCounter::GLOBAL_DELTA_TIME_CPU_SYSTEM),
        TIME_CPU_USER = NonRootCounterValue(NonRootCounter::GLOBAL_DELTA_TIME_CPU_USER)
    };
}

#endif /* INCLUDE_NON_ROOT_GLOBALCOUNTER_H */
