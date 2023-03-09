/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#include "barman-external-dependencies.h"
#include "barman-intrinsics.h"

BM_WEAK bm_uintptr barman_ext_disable_interrupts_local(void)
{
    bm_uintptr result;

#if BM_ARM_ARCH_PROFILE == 'M'
    BM_READ_SPECIAL_REG(FAULTMASK, result);
    asm ("CPSID f");
#else
    BM_READ_SPECIAL_REG(CPSR, result);
    asm ("CPSID if");
#endif

    return result;
}

BM_WEAK void barman_ext_enable_interrupts_local(bm_uintptr previous_state)
{
#if BM_ARM_ARCH_PROFILE == 'M'
    BM_WRITE_SPECIAL_REG(FAULTMASK, previous_state);
#else
    BM_WRITE_SPECIAL_REG(CPSR_c, previous_state);
#endif
}

#if BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_ITM
#define BM_USER_SUPPLIED_TIMESTAMP 0
#else
#define BM_USER_SUPPLIED_TIMESTAMP 1
#endif

#if !BM_USER_SUPPLIED_TIMESTAMP
BM_WEAK bm_uint64 barman_ext_get_timestamp(void)
{
    /* arbitrary unused value */
    return 0;
}
#endif
