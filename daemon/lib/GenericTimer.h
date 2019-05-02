/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_GENERIC_TIMER_H
#define INCLUDE_LIB_GENERIC_TIMER_H

#include <cstdint>

namespace lib
{
    inline std::uint64_t get_cntfreq_el0()
    {
#if defined(__aarch64__)
        unsigned long frequency;
        asm volatile ("mrs %0, CNTFRQ_EL0" : "=r"(frequency));
        return frequency;
#elif defined(__arm__)
        unsigned long r1;
        asm volatile ("mrc p15, 0, %0, c14, c0, 0" : "=r"(r1));
        return r1;
#else
        return 0;
#endif
    }

    inline std::uint64_t get_cntvct_el0()
    {
#if defined(__aarch64__)
        unsigned long vcount;
        asm volatile ("mrs %0, CNTVCT_EL0" : "=r"(vcount));
        return vcount;
#elif defined(__arm__)
        unsigned long r1, r2;
        asm volatile ("mrrc p15, 1, %0, %1, c14" : "=r"(r1), "=r"(r2));
        return (std::uint64_t(r2) << 32) | std::uint64_t(r1);
#else
        return 0;
#endif
    }
}

#endif /* INCLUDE_LIB_GENERIC_TIMER_H */
