/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#include "barman-cache.h"
#include "barman-intrinsics.h"

void barman_cache_clean(void * pointer, bm_uintptr length)
{
    const bm_uintptr last_address = ((bm_uintptr) pointer) + length;

    bm_uintptr ctr_val;
    bm_uintptr cache_line_size;
    bm_uintptr aligned_address;

    /* validate args */
    if (length == 0) {
        return;
    }

    /* read cache type */
    asm volatile("MRS %0, CTR_EL0" : "=r" (ctr_val));

    /* get minimum data cache line */
    cache_line_size = 4 << ((ctr_val >> 16) & 15);
    if (cache_line_size == 0) {
        return;
    }

    /* align the starting address */
    aligned_address = ((bm_uintptr) pointer) & ~(cache_line_size - 1);

    /* clean cache lines */
    while (aligned_address < last_address) {
        asm volatile ("DC CVAC, %0" :: "r" (aligned_address));
        aligned_address += cache_line_size;
    }

    barman_dsb();
}
