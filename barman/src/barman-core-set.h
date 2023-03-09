/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_CORE_SET
#define INCLUDE_BARMAN_CORE_SET

#include "barman-types.h"
#include "barman-memutils.h"

/**
 * @defgroup    bm_core_set Core set type
 * @{ */

/** Structure that forms a bitmask where each bit represents the `n`th core. A set bit indicates the core is excluded from the configuration */
typedef bm_uint8 bm_core_set[((BM_CONFIG_MAX_CORES + 7) / 8)];

/**
 * @brief   Determine if a core is in the set.
 * @param   disallowed_cores    The value to examine
 * @param   core                The core number to check
 * @return  BM_TRUE if marked in the set, BM_FALSE otherwise
 */
static BM_INLINE bm_bool barman_core_set_is_set(const bm_core_set core_set, bm_uint32 core)
{
    const bm_uint32 byte = core >> 3;
    const bm_uint32 bit = core & 7;

    if (core_set == BM_NULL) {
        return BM_FALSE;
    }

    if (byte >= sizeof(bm_core_set)) {
        return BM_FALSE;
    }

    return (core_set[byte] & BM_BIT(bit)) != 0;
}

/** @} */

#endif /* INCLUDE_BARMAN_CORE_SET */

