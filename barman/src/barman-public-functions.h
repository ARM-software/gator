/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_PUBLIC_FUNCTIONS
#define INCLUDE_BARMAN_PUBLIC_FUNCTIONS

#include "barman-types.h"
#include "barman-intrinsics.h"
#include "barman-config.h"
#if BM_ARM_ARCH_PROFILE == 'M'
#include "m-profile/barman-arch-constants.h"
#endif

/**
 * @defgroup    bm_public_functions Various public functions
 * @{
 */

/**
 * @brief   Read the MIDR value
 * @return  The MIDR value
 */
static BM_INLINE bm_uint32 barman_midr(void)
{
#if BM_ARM_ARCH_PROFILE == 'M'
    return BM_MEMORY_MAPPED_REGISTER_32(BM_CPUID_ADDRESS);
#else
    bm_uintptr val;
    BM_READ_SYS_REG(0, 0, 0, 0, val); /* MIDR */
    return val;
#endif
}

/**
 * @brief   Read the MPIDR value
 * @return  The MPIDR value
 */
static BM_INLINE bm_uintptr barman_mpidr(void)
{
#if BM_ARM_ARCH_PROFILE == 'M'
#if BM_CONFIG_MAX_CORES == 1
    return 0;
#else
#error "Multi core M profile not supported"
#endif
#else
    bm_uintptr val;
    BM_READ_SYS_REG(0, 0, 0, 5, val); /* MPIDR */
    return val;
#endif
}

/** @} */

#endif /* INCLUDE_BARMAN_PUBLIC_FUNCTIONS */
