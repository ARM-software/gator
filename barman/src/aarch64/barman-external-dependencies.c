/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#include "barman-external-dependencies.h"
#include "barman-types.h"

BM_WEAK bm_uintptr barman_ext_disable_interrupts_local(void)
{
    bm_uintptr result;

    asm volatile("MRS %0, DAIF" : "=r" (result));
    asm volatile("MSR DAIFSET, #2");

    return result;
}

BM_WEAK void barman_ext_enable_interrupts_local(bm_uintptr previous_state)
{
    asm volatile("MSR DAIF, %0" :: "r" (previous_state));
}
