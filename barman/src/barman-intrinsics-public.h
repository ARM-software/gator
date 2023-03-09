/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_INTRINSICS_PUBLIC
#define INCLUDE_BARMAN_INTRINSICS_PUBLIC

#include "barman-types-public.h"

/**
 * @defgroup    bm_intrinsics   Aliases for intrinsic assembler operations
 * @{
 *
 * @def         barman_wfi_intrinsic
 * @brief       Wait for interrupt
 * @details     Inserts an "WFI" instruction
 *
 * @def         barman_wfe_intrinsic
 * @brief       Wait for event
 * @details     Inserts an "WFE" instruction
 */

#if !BM_ARM_TARGET_ARCH_IS_UNKNOWN()

#define barman_wfi_intrinsic()    asm volatile("wfi")
#define barman_wfe_intrinsic()    asm volatile("wfe")

#else /* for unit tests */

extern void barman_wfi_intrinsic(void);
extern void barman_wfe_intrinsic(void);

#endif

/** @} */

#endif /* INCLUDE_BARMAN_INTRINSICS_PUBLIC */
