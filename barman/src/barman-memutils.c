/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#include "barman-memutils.h"
#include "barman-config.h"

#if !BM_CONFIG_ENABLE_BUILTIN_MEMFUNCS

void * barman_memset(void * ptr, int value, bm_size_t num)
{
    bm_uint8 * data = (bm_uint8 *) ptr;

    if (num > 0) {
        do {
            *data++ = value;
        } while (--num > 0);
    }

    return ptr;
}

void * barman_memcpy(void * dest, const void * src, bm_size_t num)
{
    bm_size_t i;
    for (i = 0; i < num; ++i)
    {
        ((bm_uint8 *) dest)[i] = ((bm_uint8 *) src)[i];
    }
    return dest;
}

#endif
