/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_PMCEID_H
#define INCLUDE_LIB_PMCEID_H

#include <cstdint>

namespace lib
{
    inline unsigned long get_pmceid0_el0()
    {
#if defined(__aarch64__)
        unsigned long id;
        asm volatile ("mrs %0, PMCEID0_EL0" : "=r"(id));
        return id;
#elif defined(__arm__)
        unsigned long r1;
        asm volatile ("mrc p15, 0, %0, c9, c12, 6" : "=r"(r1));
        return r1;
#else
        return 0;
#endif
    }

    inline unsigned long get_pmceid1_el0()
    {
#if defined(__aarch64__)
        unsigned long id;
        asm volatile ("mrs %0, PMCEID1_EL0" : "=r"(id));
        return id;
#elif defined(__arm__)
        unsigned long r1;
        asm volatile ("mrc p15, 0, %0, c9, c12, 7" : "=r"(r1));
        return r1;
#else
        return 0;
#endif
    }
}

#endif /* INCLUDE_LIB_PMCEID_H */
