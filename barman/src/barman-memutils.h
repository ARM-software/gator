/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_MEMUTILS
#define INCLUDE_BARMAN_MEMUTILS

#include "barman-types.h"

/**
 * @defgroup    bm_memutils Memory utility functions
 * @{ */


#if BM_CONFIG_ENABLE_BUILTIN_MEMFUNCS

/**
 * @brief   Implements `memset` functionality
 */
#define barman_memset(ptr, value, num) __builtin_memset(ptr, value, num)

/**
 * @brief   Implements `memcpy` functionality
 */
#define barman_memcpy(dest, src, count) __builtin_memcpy(dest, src, count)

#else

/**
 * @brief   Implements `memset` functionality
 */
BM_RET_NONNULL
BM_NONNULL((1))
void * barman_memset(void * ptr, int value, bm_size_t num);

/**
 * @brief   Implements `memcpy` functionality
 */
BM_RET_NONNULL
BM_NONNULL((1,2))
void * barman_memcpy(void * dest, const void * src, bm_size_t num);

#endif

/** @} */

#endif /* INCLUDE_BARMAN_MEMUTILS */
