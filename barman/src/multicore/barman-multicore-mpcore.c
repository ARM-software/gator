/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief   Contains an implementation of the barman-multicore.h interface for
 *          MPCore systems.
 * @details This implementation is suitable for implementations where the barman
 *          agent runs entirely in privileged mode and can use MPIDR register
 *          to uniquely identify a processor.
 */

#include "multicore/barman-multicore.h"
#include "barman-public-functions.h"
#include "barman-external-dependencies.h"
#include "barman-atomics.h"

/** @{ */
#define MPIDR_M_BIT         (BM_UINTPTR(1) << 31)
#define MPIDR_U_BIT         (BM_UINTPTR(1) << 30)
/** @} */

bm_uint32 barman_get_core_no(void)
{
    const bm_uintptr mpidr_value = barman_mpidr();

    if ((mpidr_value & MPIDR_M_BIT) && (mpidr_value & MPIDR_U_BIT)) {
        return 0;
    }

    return barman_ext_map_multiprocessor_affinity_to_core_no(mpidr_value);
}

#if BM_CONFIG_MAX_CORES == 1

#define BM_INVALID_MPIDR   ~BM_UINTPTR(0)

bm_uintptr first_mpidr = BM_INVALID_MPIDR;

bm_uint32 barman_ext_map_multiprocessor_affinity_to_core_no(bm_uintptr mpidr)
{
    bm_uintptr old_val = BM_INVALID_MPIDR;
    if (first_mpidr == mpidr || barman_atomic_cmp_ex_strong_pointer(&first_mpidr, &old_val, mpidr))
    {
        return 0;
    }
    else if (old_val == mpidr)
    {
        return 0;
    }
    else
    {
        return 1; /* An invalid value, doesn't matter what as long as not 0 */
    }
}

bm_uint32 barman_ext_map_multiprocessor_affinity_to_cluster_no(bm_uintptr mpidr)
{
    return 0;
}

#endif /* BM_CONFIG_MAX_CORES == 1 */
