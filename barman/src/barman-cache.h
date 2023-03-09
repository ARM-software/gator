/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_CACHE
#define INCLUDE_BARMAN_CACHE

#include "barman-types.h"

/**
 * @defgroup    bm_cache  Cache operations
 * @{
 */

/**
 * @brief   Clean some area of memory from the cache
 * @param   pointer The pointer to the start of the area to clean
 * @param   length  The length of the area to clean
 */
BM_NONNULL((1))
extern void barman_cache_clean(void * pointer, bm_uintptr length);

/** @} */

#endif /* INCLUDE_BARMAN_CACHE */
