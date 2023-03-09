/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_MULTICORE
#define INCLUDE_BARMAN_MULTICORE

#include "barman-types-public.h"

/**
 * @defgroup    bm_multicore    Multicore related functions
 * @{ */

/**
 * @brief   Detects the core number for the current core.
 * @details Returns a number which uniquely identifies the current core. The
 *          value returned will be between `[0, N)` where `N` is the number of
 *          cores on the system.
 * @note    The value returned may not be in the range `[0, BM_CONFIG_MAX_CORES)`
 *          and so any code using the value returned here must bounds check the
 *          result if they are expecting a value within this range.
 * @return  The core number
 */
bm_uint32 barman_get_core_no(void);

/** @} */

#endif /* INCLUDE_BARMAN_MULTICORE */
