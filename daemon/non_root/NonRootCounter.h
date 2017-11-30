/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_NONROOTCOUNTER_H
#define INCLUDE_NON_ROOT_NONROOTCOUNTER_H

#include <type_traits>

namespace non_root
{
    enum class NonRootCounter : unsigned
    {
        ACTIVITY_SYSTEM,
        ACTIVITY_USER,

        GLOBAL_ABS_LOADAVG_1_MINUTE,
        GLOBAL_ABS_LOADAVG_5_MINUTES,
        GLOBAL_ABS_LOADAVG_15_MINUTES,
        GLOBAL_ABS_NUM_PROCESSES_EXISTING,
        GLOBAL_ABS_NUM_PROCESSES_RUNNING,

        GLOBAL_DELTA_NUM_CONTEXT_SWITCHES,
        GLOBAL_DELTA_NUM_FORKS,
        GLOBAL_DELTA_NUM_IRQ,
        GLOBAL_DELTA_NUM_SOFTIRQ,
        GLOBAL_DELTA_TIME_CPU_GUEST_NICE,
        GLOBAL_DELTA_TIME_CPU_GUEST,
        GLOBAL_DELTA_TIME_CPU_IDLE,
        GLOBAL_DELTA_TIME_CPU_IOWAIT,
        GLOBAL_DELTA_TIME_CPU_IRQ,
        GLOBAL_DELTA_TIME_CPU_NICE,
        GLOBAL_DELTA_TIME_CPU_SOFTIRQ,
        GLOBAL_DELTA_TIME_CPU_STEAL,
        GLOBAL_DELTA_TIME_CPU_SYSTEM,
        GLOBAL_DELTA_TIME_CPU_USER,

        PROCESS_ABS_DATA_SIZE,
        PROCESS_ABS_NUM_THREADS,
        PROCESS_ABS_RES_LIMIT,
        PROCESS_ABS_RES_SIZE,
        PROCESS_ABS_SHARED_SIZE,
        PROCESS_ABS_TEXT_SIZE,
        PROCESS_ABS_VM_SIZE,

        PROCESS_DELTA_MAJOR_FAULTS,
        PROCESS_DELTA_MINOR_FAULTS,
        PROCESS_DELTA_UTIME,
        PROCESS_DELTA_STIME,
        PROCESS_DELTA_GUEST_TIME
    };

    static constexpr typename std::underlying_type<NonRootCounter>::type NonRootCounterValue(NonRootCounter nrc)
    {
        return static_cast<typename std::underlying_type<NonRootCounter>::type>(nrc);
    }
}

#endif /* INCLUDE_NON_ROOT_NONROOTCOUNTER_H */


