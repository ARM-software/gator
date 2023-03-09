/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

/* We define our own to reduce dependency on external headers, and to avoid
 * conflicts with any definitions in other headers where barman headers
 * are included */

#ifndef INCLUDE_BARMAN_PMU
#define INCLUDE_BARMAN_PMU

#include "barman-intrinsics.h"

/**
 * @defgroup    bm_pmu_intrinsics   Macros for reading and writing PMU registers
 * @{
 *
 * @def         BM_PMU_AT_LEAST_V2
 * @brief       Defines whether targeting PMUv2 or later
 *
 * @def         BM_PMU_AT_LEAST_V3
 * @brief       Defines whether targeting PMUv3 or later
 *
 * @def         BM_PMU_AT_LEAST_V3_1
 * @brief       Defines whether targeting PMUv3 with ARMv8.1 extension or later
 *
 * @def         BM_READ_PMxx(out)
 * @brief       Read from PMU register PMxx (or register that architecturally maps to it)
 * @param       out Type "bm_uinptr"
 *              L-value to write the result into.
 *
 * @def         BM_WRITE_PMxx(in)
 * @brief       Write to PMU register PMxx (or register that architecturally maps to it)
 * @param       in Type "bm_uinptr"
 *              Value to write into the register.
 *
 * @def         BM_READ_PMxx_64(out)
 * @brief       Read 64 bits from PMU register PMxx (or register that architecturally maps to it)
 * @param       out Type "bm_uint64"
 *              L-value to write the result into.
 *
 * @def         BM_WRITE_PMxx_64(in)
 * @brief       Write 64 bits to PMU register PMxx (or register that architecturally maps to it)
 * @param       in Type "bm_uint64"
 *              Value to write into the register.
 *
 * @}
 */

/** @{ */

#define BM_PMU_AT_LEAST_V2   1
#define BM_PMU_AT_LEAST_V3   (BM_ARM_TARGET_ARCH >= 8)
#define BM_PMU_AT_LEAST_V3_1 (BM_ARM_TARGET_ARCH >= 801)

#if BM_PMU_AT_LEAST_V2

#if BM_ARM_32BIT_STATE

#define BM_READ_PMCR(x)          BM_READ_SYS_REG(0,  9, 12, 0, x)
#define BM_READ_PMCNTENSET(x)    BM_READ_SYS_REG(0,  9, 12, 1, x)
#define BM_READ_PMCNTENCLR(x)    BM_READ_SYS_REG(0,  9, 12, 2, x)
#define BM_READ_PMOVSR(x)        BM_READ_SYS_REG(0,  9, 12, 3, x)
  /* WRITE ONLY PMSWINC */
#define BM_READ_PMSELR(x)        BM_READ_SYS_REG(0,  9, 12, 5, x)
#define BM_READ_PMCEID0(x)       BM_READ_SYS_REG(0,  9, 12, 6, x)
#define BM_READ_PMCEID1(x)       BM_READ_SYS_REG(0,  9, 12, 7, x)

#define BM_READ_PMCCNTR(x)       BM_READ_SYS_REG(0,  9, 13, 0, x)
#define BM_READ_PMXEVTYPER(x)    BM_READ_SYS_REG(0,  9, 13, 1, x)
#define BM_READ_PMXEVCNTR(x)     BM_READ_SYS_REG(0,  9, 13, 2, x)

#define BM_READ_PMUSERENR(x)     BM_READ_SYS_REG(0,  9, 14, 0, x)
#define BM_READ_PMINTENSET(x)    BM_READ_SYS_REG(0,  9, 14, 1, x)
#define BM_READ_PMINTENCLR(x)    BM_READ_SYS_REG(0,  9, 14, 2, x)
#define BM_READ_PMOVSSET(x)      BM_READ_SYS_REG(0,  9, 14, 3, x)

#define BM_WRITE_PMCR(x)         BM_WRITE_SYS_REG(0,  9, 12, 0, x)
#define BM_WRITE_PMCNTENSET(x)   BM_WRITE_SYS_REG(0,  9, 12, 1, x)
#define BM_WRITE_PMCNTENCLR(x)   BM_WRITE_SYS_REG(0,  9, 12, 2, x)
#define BM_WRITE_PMOVSR(x)       BM_WRITE_SYS_REG(0,  9, 12, 3, x)
#define BM_WRITE_PMSWINC(x)      BM_WRITE_SYS_REG(0,  9, 12, 4, x)
#define BM_WRITE_PMSELR(x)       BM_WRITE_SYS_REG(0,  9, 12, 5, x)
    /* READ ONLY PMCEID0 */
    /* READ ONLY PMCEID1 */

#define BM_WRITE_PMCCNTR(x)      BM_WRITE_SYS_REG(0,  9, 13, 0, x)
#define BM_WRITE_PMXEVTYPER(x)   BM_WRITE_SYS_REG(0,  9, 13, 1, x)
#define BM_WRITE_PMXEVCNTR(x)    BM_WRITE_SYS_REG(0,  9, 13, 2, x)

#define BM_WRITE_PMUSERENR(x)    BM_WRITE_SYS_REG(0,  9, 14, 0, x)
#define BM_WRITE_PMINTENSET(x)   BM_WRITE_SYS_REG(0,  9, 14, 1, x)
#define BM_WRITE_PMINTENCLR(x)   BM_WRITE_SYS_REG(0,  9, 14, 2, x)
#define BM_WRITE_PMOVSSET(x)     BM_WRITE_SYS_REG(0,  9, 14, 3, x)

#elif BM_ARM_64BIT_STATE

#define BM_READ_PMCR(x)          BM_READ_SYS_REG_NAMED(PMCR_EL0,       x)
#define BM_READ_PMCNTENSET(x)    BM_READ_SYS_REG_NAMED(PMCNTENSET_EL0, x)
#define BM_READ_PMCNTENCLR(x)    BM_READ_SYS_REG_NAMED(PMCNTENCLR_EL0, x)
#define BM_READ_PMOVSR(x)        BM_READ_SYS_REG_NAMED(PMOVSCLR_EL0,   x)
  /* WRITE ONLY PMSWINC */
#define BM_READ_PMSELR(x)        BM_READ_SYS_REG_NAMED(PMSELR_EL0,     x)
#define BM_READ_PMCEID0(x)       BM_READ_SYS_REG_NAMED(PMCEID0_EL0,    x)
#define BM_READ_PMCEID1(x)       BM_READ_SYS_REG_NAMED(PMCEID1_EL0,    x)

#define BM_READ_PMCCNTR(x)       BM_READ_SYS_REG_NAMED(PMCCNTR_EL0,    x)
#define BM_READ_PMXEVTYPER(x)    BM_READ_SYS_REG_NAMED(PMXEVTYPER_EL0, x)
#define BM_READ_PMXEVCNTR(x)     BM_READ_SYS_REG_NAMED(PMXEVCNTR_EL0,  x)

#define BM_READ_PMUSERENR(x)     BM_READ_SYS_REG_NAMED(PMUSERENR_EL0,  x)
#define BM_READ_PMINTENSET(x)    BM_READ_SYS_REG_NAMED(PMINTENSET_EL1, x)
#define BM_READ_PMINTENCLR(x)    BM_READ_SYS_REG_NAMED(PMINTENCLR_EL1, x)
#define BM_READ_PMOVSSET(x)      BM_READ_SYS_REG_NAMED(PMOVSSET_EL0,   x)

#define BM_WRITE_PMCR(x)         BM_WRITE_SYS_REG_NAMED(PMCR_EL0,       x)
#define BM_WRITE_PMCNTENSET(x)   BM_WRITE_SYS_REG_NAMED(PMCNTENSET_EL0, x)
#define BM_WRITE_PMCNTENCLR(x)   BM_WRITE_SYS_REG_NAMED(PMCNTENCLR_EL0, x)
#define BM_WRITE_PMOVSR(x)       BM_WRITE_SYS_REG_NAMED(PMOVSCLR_EL0,   x)
#define BM_WRITE_PMSWINC(x)      BM_WRITE_SYS_REG_NAMED(PMSWINC_EL0,    x)
#define BM_WRITE_PMSELR(x)       BM_WRITE_SYS_REG_NAMED(PMSELR_EL0,     x)
    /* READ ONLY PMCEID0 */
    /* READ ONLY PMCEID1 */

#define BM_WRITE_PMCCNTR(x)      BM_WRITE_SYS_REG_NAMED(PMCCNTR_EL0,    x)
#define BM_WRITE_PMXEVTYPER(x)   BM_WRITE_SYS_REG_NAMED(PMXEVTYPER_EL0, x)
#define BM_WRITE_PMXEVCNTR(x)    BM_WRITE_SYS_REG_NAMED(PMXEVCNTR_EL0,  x)

#define BM_WRITE_PMUSERENR(x)    BM_WRITE_SYS_REG_NAMED(PMUSERENR_EL0,  x)
#define BM_WRITE_PMINTENSET(x)   BM_WRITE_SYS_REG_NAMED(PMINTENSET_EL1, x)
#define BM_WRITE_PMINTENCLR(x)   BM_WRITE_SYS_REG_NAMED(PMINTENCLR_EL1, x)
#define BM_WRITE_PMOVSSET(x)     BM_WRITE_SYS_REG_NAMED(PMOVSSET_EL0,   x)

#endif

/* NOT reentrant */
#define BM_READ_PMEVCNTR_NR(n, x)    do { BM_WRITE_PMSELR(n);  barman_isb(); BM_READ_PMXEVCNTR(x);   } while (0)
#define BM_READ_PMEVTYPER_NR(n, x)   do { BM_WRITE_PMSELR(n);  barman_isb(); BM_READ_PMXEVTYPER(x);  } while (0)
#define BM_READ_PMCCFILTR_NR(x)      do { BM_WRITE_PMSELR(31); barman_isb(); BM_READ_PMXEVTYPER(x);  } while (0)
#define BM_WRITE_PMEVCNTR_NR(n, x)   do { BM_WRITE_PMSELR(n);  barman_isb(); BM_WRITE_PMXEVCNTR(x);  } while (0)
#define BM_WRITE_PMEVTYPER_NR(n, x)  do { BM_WRITE_PMSELR(n);  barman_isb(); BM_WRITE_PMXEVTYPER(x); } while (0)
#define BM_WRITE_PMCCFILTR_NR(x)     do { BM_WRITE_PMSELR(31); barman_isb(); BM_WRITE_PMXEVTYPER(x); } while (0)

#endif /* BM_PMU_AT_LEAST_V2 */

#if BM_PMU_AT_LEAST_V3

#if BM_ARM_32BIT_STATE

#define BM_READ_PMEVCNTR0(x)      BM_READ_SYS_REG(0, 14,  8, 0, x)
#define BM_READ_PMEVCNTR1(x)      BM_READ_SYS_REG(0, 14,  8, 1, x)
#define BM_READ_PMEVCNTR2(x)      BM_READ_SYS_REG(0, 14,  8, 2, x)
#define BM_READ_PMEVCNTR3(x)      BM_READ_SYS_REG(0, 14,  8, 3, x)
#define BM_READ_PMEVCNTR4(x)      BM_READ_SYS_REG(0, 14,  8, 4, x)
#define BM_READ_PMEVCNTR5(x)      BM_READ_SYS_REG(0, 14,  8, 5, x)
#define BM_READ_PMEVCNTR6(x)      BM_READ_SYS_REG(0, 14,  8, 6, x)
#define BM_READ_PMEVCNTR7(x)      BM_READ_SYS_REG(0, 14,  8, 7, x)

#define BM_READ_PMEVCNTR8(x)      BM_READ_SYS_REG(0, 14,  9, 0, x)
#define BM_READ_PMEVCNTR9(x)      BM_READ_SYS_REG(0, 14,  9, 1, x)
#define BM_READ_PMEVCNTR10(x)     BM_READ_SYS_REG(0, 14,  9, 2, x)
#define BM_READ_PMEVCNTR11(x)     BM_READ_SYS_REG(0, 14,  9, 3, x)
#define BM_READ_PMEVCNTR12(x)     BM_READ_SYS_REG(0, 14,  9, 4, x)
#define BM_READ_PMEVCNTR13(x)     BM_READ_SYS_REG(0, 14,  9, 5, x)
#define BM_READ_PMEVCNTR14(x)     BM_READ_SYS_REG(0, 14,  9, 6, x)
#define BM_READ_PMEVCNTR15(x)     BM_READ_SYS_REG(0, 14,  9, 7, x)

#define BM_READ_PMEVCNTR16(x)     BM_READ_SYS_REG(0, 14, 10, 0, x)
#define BM_READ_PMEVCNTR17(x)     BM_READ_SYS_REG(0, 14, 10, 1, x)
#define BM_READ_PMEVCNTR18(x)     BM_READ_SYS_REG(0, 14, 10, 2, x)
#define BM_READ_PMEVCNTR19(x)     BM_READ_SYS_REG(0, 14, 10, 3, x)
#define BM_READ_PMEVCNTR20(x)     BM_READ_SYS_REG(0, 14, 10, 4, x)
#define BM_READ_PMEVCNTR21(x)     BM_READ_SYS_REG(0, 14, 10, 5, x)
#define BM_READ_PMEVCNTR22(x)     BM_READ_SYS_REG(0, 14, 10, 6, x)
#define BM_READ_PMEVCNTR23(x)     BM_READ_SYS_REG(0, 14, 10, 7, x)

#define BM_READ_PMEVCNTR24(x)     BM_READ_SYS_REG(0, 14, 11, 0, x)
#define BM_READ_PMEVCNTR25(x)     BM_READ_SYS_REG(0, 14, 11, 1, x)
#define BM_READ_PMEVCNTR26(x)     BM_READ_SYS_REG(0, 14, 11, 2, x)
#define BM_READ_PMEVCNTR27(x)     BM_READ_SYS_REG(0, 14, 11, 3, x)
#define BM_READ_PMEVCNTR28(x)     BM_READ_SYS_REG(0, 14, 11, 4, x)
#define BM_READ_PMEVCNTR29(x)     BM_READ_SYS_REG(0, 14, 11, 5, x)
#define BM_READ_PMEVCNTR30(x)     BM_READ_SYS_REG(0, 14, 11, 6, x)

#define BM_READ_PMEVTYPER0(x)     BM_READ_SYS_REG(0, 14, 12, 0, x)
#define BM_READ_PMEVTYPER1(x)     BM_READ_SYS_REG(0, 14, 12, 1, x)
#define BM_READ_PMEVTYPER2(x)     BM_READ_SYS_REG(0, 14, 12, 2, x)
#define BM_READ_PMEVTYPER3(x)     BM_READ_SYS_REG(0, 14, 12, 3, x)
#define BM_READ_PMEVTYPER4(x)     BM_READ_SYS_REG(0, 14, 12, 4, x)
#define BM_READ_PMEVTYPER5(x)     BM_READ_SYS_REG(0, 14, 12, 5, x)
#define BM_READ_PMEVTYPER6(x)     BM_READ_SYS_REG(0, 14, 12, 6, x)
#define BM_READ_PMEVTYPER7(x)     BM_READ_SYS_REG(0, 14, 12, 7, x)

#define BM_READ_PMEVTYPER8(x)     BM_READ_SYS_REG(0, 14, 13, 0, x)
#define BM_READ_PMEVTYPER9(x)     BM_READ_SYS_REG(0, 14, 13, 1, x)
#define BM_READ_PMEVTYPER10(x)    BM_READ_SYS_REG(0, 14, 13, 2, x)
#define BM_READ_PMEVTYPER11(x)    BM_READ_SYS_REG(0, 14, 13, 3, x)
#define BM_READ_PMEVTYPER12(x)    BM_READ_SYS_REG(0, 14, 13, 4, x)
#define BM_READ_PMEVTYPER13(x)    BM_READ_SYS_REG(0, 14, 13, 5, x)
#define BM_READ_PMEVTYPER14(x)    BM_READ_SYS_REG(0, 14, 13, 6, x)
#define BM_READ_PMEVTYPER15(x)    BM_READ_SYS_REG(0, 14, 13, 7, x)

#define BM_READ_PMEVTYPER16(x)    BM_READ_SYS_REG(0, 14, 14, 0, x)
#define BM_READ_PMEVTYPER17(x)    BM_READ_SYS_REG(0, 14, 14, 1, x)
#define BM_READ_PMEVTYPER18(x)    BM_READ_SYS_REG(0, 14, 14, 2, x)
#define BM_READ_PMEVTYPER19(x)    BM_READ_SYS_REG(0, 14, 14, 3, x)
#define BM_READ_PMEVTYPER20(x)    BM_READ_SYS_REG(0, 14, 14, 4, x)
#define BM_READ_PMEVTYPER21(x)    BM_READ_SYS_REG(0, 14, 14, 5, x)
#define BM_READ_PMEVTYPER22(x)    BM_READ_SYS_REG(0, 14, 14, 6, x)
#define BM_READ_PMEVTYPER23(x)    BM_READ_SYS_REG(0, 14, 14, 7, x)

#define BM_READ_PMEVTYPER24(x)    BM_READ_SYS_REG(0, 14, 15, 0, x)
#define BM_READ_PMEVTYPER25(x)    BM_READ_SYS_REG(0, 14, 15, 1, x)
#define BM_READ_PMEVTYPER26(x)    BM_READ_SYS_REG(0, 14, 15, 2, x)
#define BM_READ_PMEVTYPER27(x)    BM_READ_SYS_REG(0, 14, 15, 3, x)
#define BM_READ_PMEVTYPER28(x)    BM_READ_SYS_REG(0, 14, 15, 4, x)
#define BM_READ_PMEVTYPER29(x)    BM_READ_SYS_REG(0, 14, 15, 5, x)
#define BM_READ_PMEVTYPER30(x)    BM_READ_SYS_REG(0, 14, 15, 6, x)
#define BM_READ_PMCCFILTR(x)      BM_READ_SYS_REG(0, 14, 15, 7, x)

#define BM_WRITE_PMEVCNTR0(x)     BM_WRITE_SYS_REG(0, 14,  8, 0, x)
#define BM_WRITE_PMEVCNTR1(x)     BM_WRITE_SYS_REG(0, 14,  8, 1, x)
#define BM_WRITE_PMEVCNTR2(x)     BM_WRITE_SYS_REG(0, 14,  8, 2, x)
#define BM_WRITE_PMEVCNTR3(x)     BM_WRITE_SYS_REG(0, 14,  8, 3, x)
#define BM_WRITE_PMEVCNTR4(x)     BM_WRITE_SYS_REG(0, 14,  8, 4, x)
#define BM_WRITE_PMEVCNTR5(x)     BM_WRITE_SYS_REG(0, 14,  8, 5, x)
#define BM_WRITE_PMEVCNTR6(x)     BM_WRITE_SYS_REG(0, 14,  8, 6, x)
#define BM_WRITE_PMEVCNTR7(x)     BM_WRITE_SYS_REG(0, 14,  8, 7, x)

#define BM_WRITE_PMEVCNTR8(x)     BM_WRITE_SYS_REG(0, 14,  9, 0, x)
#define BM_WRITE_PMEVCNTR9(x)     BM_WRITE_SYS_REG(0, 14,  9, 1, x)
#define BM_WRITE_PMEVCNTR10(x)    BM_WRITE_SYS_REG(0, 14,  9, 2, x)
#define BM_WRITE_PMEVCNTR11(x)    BM_WRITE_SYS_REG(0, 14,  9, 3, x)
#define BM_WRITE_PMEVCNTR12(x)    BM_WRITE_SYS_REG(0, 14,  9, 4, x)
#define BM_WRITE_PMEVCNTR13(x)    BM_WRITE_SYS_REG(0, 14,  9, 5, x)
#define BM_WRITE_PMEVCNTR14(x)    BM_WRITE_SYS_REG(0, 14,  9, 6, x)
#define BM_WRITE_PMEVCNTR15(x)    BM_WRITE_SYS_REG(0, 14,  9, 7, x)

#define BM_WRITE_PMEVCNTR16(x)    BM_WRITE_SYS_REG(0, 14, 10, 0, x)
#define BM_WRITE_PMEVCNTR17(x)    BM_WRITE_SYS_REG(0, 14, 10, 1, x)
#define BM_WRITE_PMEVCNTR18(x)    BM_WRITE_SYS_REG(0, 14, 10, 2, x)
#define BM_WRITE_PMEVCNTR19(x)    BM_WRITE_SYS_REG(0, 14, 10, 3, x)
#define BM_WRITE_PMEVCNTR20(x)    BM_WRITE_SYS_REG(0, 14, 10, 4, x)
#define BM_WRITE_PMEVCNTR21(x)    BM_WRITE_SYS_REG(0, 14, 10, 5, x)
#define BM_WRITE_PMEVCNTR22(x)    BM_WRITE_SYS_REG(0, 14, 10, 6, x)
#define BM_WRITE_PMEVCNTR23(x)    BM_WRITE_SYS_REG(0, 14, 10, 7, x)

#define BM_WRITE_PMEVCNTR24(x)    BM_WRITE_SYS_REG(0, 14, 11, 0, x)
#define BM_WRITE_PMEVCNTR25(x)    BM_WRITE_SYS_REG(0, 14, 11, 1, x)
#define BM_WRITE_PMEVCNTR26(x)    BM_WRITE_SYS_REG(0, 14, 11, 2, x)
#define BM_WRITE_PMEVCNTR27(x)    BM_WRITE_SYS_REG(0, 14, 11, 3, x)
#define BM_WRITE_PMEVCNTR28(x)    BM_WRITE_SYS_REG(0, 14, 11, 4, x)
#define BM_WRITE_PMEVCNTR29(x)    BM_WRITE_SYS_REG(0, 14, 11, 5, x)
#define BM_WRITE_PMEVCNTR30(x)    BM_WRITE_SYS_REG(0, 14, 11, 6, x)

#define BM_WRITE_PMEVTYPER0(x)    BM_WRITE_SYS_REG(0, 14, 12, 0, x)
#define BM_WRITE_PMEVTYPER1(x)    BM_WRITE_SYS_REG(0, 14, 12, 1, x)
#define BM_WRITE_PMEVTYPER2(x)    BM_WRITE_SYS_REG(0, 14, 12, 2, x)
#define BM_WRITE_PMEVTYPER3(x)    BM_WRITE_SYS_REG(0, 14, 12, 3, x)
#define BM_WRITE_PMEVTYPER4(x)    BM_WRITE_SYS_REG(0, 14, 12, 4, x)
#define BM_WRITE_PMEVTYPER5(x)    BM_WRITE_SYS_REG(0, 14, 12, 5, x)
#define BM_WRITE_PMEVTYPER6(x)    BM_WRITE_SYS_REG(0, 14, 12, 6, x)
#define BM_WRITE_PMEVTYPER7(x)    BM_WRITE_SYS_REG(0, 14, 12, 7, x)

#define BM_WRITE_PMEVTYPER8(x)    BM_WRITE_SYS_REG(0, 14, 13, 0, x)
#define BM_WRITE_PMEVTYPER9(x)    BM_WRITE_SYS_REG(0, 14, 13, 1, x)
#define BM_WRITE_PMEVTYPER10(x)   BM_WRITE_SYS_REG(0, 14, 13, 2, x)
#define BM_WRITE_PMEVTYPER11(x)   BM_WRITE_SYS_REG(0, 14, 13, 3, x)
#define BM_WRITE_PMEVTYPER12(x)   BM_WRITE_SYS_REG(0, 14, 13, 4, x)
#define BM_WRITE_PMEVTYPER13(x)   BM_WRITE_SYS_REG(0, 14, 13, 5, x)
#define BM_WRITE_PMEVTYPER14(x)   BM_WRITE_SYS_REG(0, 14, 13, 6, x)
#define BM_WRITE_PMEVTYPER15(x)   BM_WRITE_SYS_REG(0, 14, 13, 7, x)

#define BM_WRITE_PMEVTYPER16(x)   BM_WRITE_SYS_REG(0, 14, 14, 0, x)
#define BM_WRITE_PMEVTYPER17(x)   BM_WRITE_SYS_REG(0, 14, 14, 1, x)
#define BM_WRITE_PMEVTYPER18(x)   BM_WRITE_SYS_REG(0, 14, 14, 2, x)
#define BM_WRITE_PMEVTYPER19(x)   BM_WRITE_SYS_REG(0, 14, 14, 3, x)
#define BM_WRITE_PMEVTYPER20(x)   BM_WRITE_SYS_REG(0, 14, 14, 4, x)
#define BM_WRITE_PMEVTYPER21(x)   BM_WRITE_SYS_REG(0, 14, 14, 5, x)
#define BM_WRITE_PMEVTYPER22(x)   BM_WRITE_SYS_REG(0, 14, 14, 6, x)
#define BM_WRITE_PMEVTYPER23(x)   BM_WRITE_SYS_REG(0, 14, 14, 7, x)

#define BM_WRITE_PMEVTYPER24(x)   BM_WRITE_SYS_REG(0, 14, 15, 0, x)
#define BM_WRITE_PMEVTYPER25(x)   BM_WRITE_SYS_REG(0, 14, 15, 1, x)
#define BM_WRITE_PMEVTYPER26(x)   BM_WRITE_SYS_REG(0, 14, 15, 2, x)
#define BM_WRITE_PMEVTYPER27(x)   BM_WRITE_SYS_REG(0, 14, 15, 3, x)
#define BM_WRITE_PMEVTYPER28(x)   BM_WRITE_SYS_REG(0, 14, 15, 4, x)
#define BM_WRITE_PMEVTYPER29(x)   BM_WRITE_SYS_REG(0, 14, 15, 5, x)
#define BM_WRITE_PMEVTYPER30(x)   BM_WRITE_SYS_REG(0, 14, 15, 6, x)
#define BM_WRITE_PMCCFILTR(x)     BM_WRITE_SYS_REG(0, 14, 15, 7, x)

#define BM_READ_PMCCNTR_64(x)     BM_READ_SYS_REG_64(0, 9, x)
#define BM_WRITE_PMCCNTR_64(x)    BM_WRITE_SYS_REG_64(0, 9, x)

#elif BM_ARM_64BIT_STATE

#define BM_READ_PMEVCNTR0(x)     BM_READ_SYS_REG_NAMED(PMEVCNTR0_EL0,   x)
#define BM_READ_PMEVCNTR1(x)     BM_READ_SYS_REG_NAMED(PMEVCNTR1_EL0,   x)
#define BM_READ_PMEVCNTR2(x)     BM_READ_SYS_REG_NAMED(PMEVCNTR2_EL0,   x)
#define BM_READ_PMEVCNTR3(x)     BM_READ_SYS_REG_NAMED(PMEVCNTR3_EL0,   x)
#define BM_READ_PMEVCNTR4(x)     BM_READ_SYS_REG_NAMED(PMEVCNTR4_EL0,   x)
#define BM_READ_PMEVCNTR5(x)     BM_READ_SYS_REG_NAMED(PMEVCNTR5_EL0,   x)
#define BM_READ_PMEVCNTR6(x)     BM_READ_SYS_REG_NAMED(PMEVCNTR6_EL0,   x)
#define BM_READ_PMEVCNTR7(x)     BM_READ_SYS_REG_NAMED(PMEVCNTR7_EL0,   x)

#define BM_READ_PMEVCNTR8(x)     BM_READ_SYS_REG_NAMED(PMEVCNTR8_EL0,   x)
#define BM_READ_PMEVCNTR9(x)     BM_READ_SYS_REG_NAMED(PMEVCNTR9_EL0,   x)
#define BM_READ_PMEVCNTR10(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR10_EL0,  x)
#define BM_READ_PMEVCNTR11(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR11_EL0,  x)
#define BM_READ_PMEVCNTR12(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR12_EL0,  x)
#define BM_READ_PMEVCNTR13(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR13_EL0,  x)
#define BM_READ_PMEVCNTR14(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR14_EL0,  x)
#define BM_READ_PMEVCNTR15(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR15_EL0,  x)

#define BM_READ_PMEVCNTR16(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR16_EL0,  x)
#define BM_READ_PMEVCNTR17(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR17_EL0,  x)
#define BM_READ_PMEVCNTR18(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR18_EL0,  x)
#define BM_READ_PMEVCNTR19(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR19_EL0,  x)
#define BM_READ_PMEVCNTR20(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR20_EL0,  x)
#define BM_READ_PMEVCNTR21(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR21_EL0,  x)
#define BM_READ_PMEVCNTR22(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR22_EL0,  x)
#define BM_READ_PMEVCNTR23(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR23_EL0,  x)

#define BM_READ_PMEVCNTR24(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR24_EL0,  x)
#define BM_READ_PMEVCNTR25(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR25_EL0,  x)
#define BM_READ_PMEVCNTR26(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR26_EL0,  x)
#define BM_READ_PMEVCNTR27(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR27_EL0,  x)
#define BM_READ_PMEVCNTR28(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR28_EL0,  x)
#define BM_READ_PMEVCNTR29(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR29_EL0,  x)
#define BM_READ_PMEVCNTR30(x)    BM_READ_SYS_REG_NAMED(PMEVCNTR30_EL0,  x)

#define BM_READ_PMEVTYPER0(x)    BM_READ_SYS_REG_NAMED(PMEVTYPER0_EL0,  x)
#define BM_READ_PMEVTYPER1(x)    BM_READ_SYS_REG_NAMED(PMEVTYPER1_EL0,  x)
#define BM_READ_PMEVTYPER2(x)    BM_READ_SYS_REG_NAMED(PMEVTYPER2_EL0,  x)
#define BM_READ_PMEVTYPER3(x)    BM_READ_SYS_REG_NAMED(PMEVTYPER3_EL0,  x)
#define BM_READ_PMEVTYPER4(x)    BM_READ_SYS_REG_NAMED(PMEVTYPER4_EL0,  x)
#define BM_READ_PMEVTYPER5(x)    BM_READ_SYS_REG_NAMED(PMEVTYPER5_EL0,  x)
#define BM_READ_PMEVTYPER6(x)    BM_READ_SYS_REG_NAMED(PMEVTYPER6_EL0,  x)
#define BM_READ_PMEVTYPER7(x)    BM_READ_SYS_REG_NAMED(PMEVTYPER7_EL0,  x)

#define BM_READ_PMEVTYPER8(x)    BM_READ_SYS_REG_NAMED(PMEVTYPER8_EL0,  x)
#define BM_READ_PMEVTYPER9(x)    BM_READ_SYS_REG_NAMED(PMEVTYPER9_EL0,  x)
#define BM_READ_PMEVTYPER10(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER10_EL0, x)
#define BM_READ_PMEVTYPER11(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER11_EL0, x)
#define BM_READ_PMEVTYPER12(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER12_EL0, x)
#define BM_READ_PMEVTYPER13(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER13_EL0, x)
#define BM_READ_PMEVTYPER14(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER14_EL0, x)
#define BM_READ_PMEVTYPER15(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER15_EL0, x)

#define BM_READ_PMEVTYPER16(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER16_EL0, x)
#define BM_READ_PMEVTYPER17(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER17_EL0, x)
#define BM_READ_PMEVTYPER18(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER18_EL0, x)
#define BM_READ_PMEVTYPER19(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER19_EL0, x)
#define BM_READ_PMEVTYPER20(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER20_EL0, x)
#define BM_READ_PMEVTYPER21(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER21_EL0, x)
#define BM_READ_PMEVTYPER22(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER22_EL0, x)
#define BM_READ_PMEVTYPER23(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER23_EL0, x)

#define BM_READ_PMEVTYPER24(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER24_EL0, x)
#define BM_READ_PMEVTYPER25(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER25_EL0, x)
#define BM_READ_PMEVTYPER26(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER26_EL0, x)
#define BM_READ_PMEVTYPER27(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER27_EL0, x)
#define BM_READ_PMEVTYPER28(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER28_EL0, x)
#define BM_READ_PMEVTYPER29(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER29_EL0, x)
#define BM_READ_PMEVTYPER30(x)   BM_READ_SYS_REG_NAMED(PMEVTYPER30_EL0, x)
#define BM_READ_PMCCFILTR(x)     BM_READ_SYS_REG_NAMED(PMCCFILTR_EL0,   x)

#define BM_WRITE_PMEVCNTR0(x)    BM_WRITE_SYS_REG_NAMED(PMEVCNTR0_EL0,   x)
#define BM_WRITE_PMEVCNTR1(x)    BM_WRITE_SYS_REG_NAMED(PMEVCNTR1_EL0,   x)
#define BM_WRITE_PMEVCNTR2(x)    BM_WRITE_SYS_REG_NAMED(PMEVCNTR2_EL0,   x)
#define BM_WRITE_PMEVCNTR3(x)    BM_WRITE_SYS_REG_NAMED(PMEVCNTR3_EL0,   x)
#define BM_WRITE_PMEVCNTR4(x)    BM_WRITE_SYS_REG_NAMED(PMEVCNTR4_EL0,   x)
#define BM_WRITE_PMEVCNTR5(x)    BM_WRITE_SYS_REG_NAMED(PMEVCNTR5_EL0,   x)
#define BM_WRITE_PMEVCNTR6(x)    BM_WRITE_SYS_REG_NAMED(PMEVCNTR6_EL0,   x)
#define BM_WRITE_PMEVCNTR7(x)    BM_WRITE_SYS_REG_NAMED(PMEVCNTR7_EL0,   x)

#define BM_WRITE_PMEVCNTR8(x)    BM_WRITE_SYS_REG_NAMED(PMEVCNTR8_EL0,   x)
#define BM_WRITE_PMEVCNTR9(x)    BM_WRITE_SYS_REG_NAMED(PMEVCNTR9_EL0,   x)
#define BM_WRITE_PMEVCNTR10(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR10_EL0,  x)
#define BM_WRITE_PMEVCNTR11(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR11_EL0,  x)
#define BM_WRITE_PMEVCNTR12(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR12_EL0,  x)
#define BM_WRITE_PMEVCNTR13(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR13_EL0,  x)
#define BM_WRITE_PMEVCNTR14(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR14_EL0,  x)
#define BM_WRITE_PMEVCNTR15(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR15_EL0,  x)

#define BM_WRITE_PMEVCNTR16(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR16_EL0,  x)
#define BM_WRITE_PMEVCNTR17(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR17_EL0,  x)
#define BM_WRITE_PMEVCNTR18(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR18_EL0,  x)
#define BM_WRITE_PMEVCNTR19(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR19_EL0,  x)
#define BM_WRITE_PMEVCNTR20(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR20_EL0,  x)
#define BM_WRITE_PMEVCNTR21(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR21_EL0,  x)
#define BM_WRITE_PMEVCNTR22(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR22_EL0,  x)
#define BM_WRITE_PMEVCNTR23(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR23_EL0,  x)

#define BM_WRITE_PMEVCNTR24(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR24_EL0,  x)
#define BM_WRITE_PMEVCNTR25(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR25_EL0,  x)
#define BM_WRITE_PMEVCNTR26(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR26_EL0,  x)
#define BM_WRITE_PMEVCNTR27(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR27_EL0,  x)
#define BM_WRITE_PMEVCNTR28(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR28_EL0,  x)
#define BM_WRITE_PMEVCNTR29(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR29_EL0,  x)
#define BM_WRITE_PMEVCNTR30(x)   BM_WRITE_SYS_REG_NAMED(PMEVCNTR30_EL0,  x)

#define BM_WRITE_PMEVTYPER0(x)   BM_WRITE_SYS_REG_NAMED(PMEVTYPER0_EL0,  x)
#define BM_WRITE_PMEVTYPER1(x)   BM_WRITE_SYS_REG_NAMED(PMEVTYPER1_EL0,  x)
#define BM_WRITE_PMEVTYPER2(x)   BM_WRITE_SYS_REG_NAMED(PMEVTYPER2_EL0,  x)
#define BM_WRITE_PMEVTYPER3(x)   BM_WRITE_SYS_REG_NAMED(PMEVTYPER3_EL0,  x)
#define BM_WRITE_PMEVTYPER4(x)   BM_WRITE_SYS_REG_NAMED(PMEVTYPER4_EL0,  x)
#define BM_WRITE_PMEVTYPER5(x)   BM_WRITE_SYS_REG_NAMED(PMEVTYPER5_EL0,  x)
#define BM_WRITE_PMEVTYPER6(x)   BM_WRITE_SYS_REG_NAMED(PMEVTYPER6_EL0,  x)
#define BM_WRITE_PMEVTYPER7(x)   BM_WRITE_SYS_REG_NAMED(PMEVTYPER7_EL0,  x)

#define BM_WRITE_PMEVTYPER8(x)   BM_WRITE_SYS_REG_NAMED(PMEVTYPER8_EL0,  x)
#define BM_WRITE_PMEVTYPER9(x)   BM_WRITE_SYS_REG_NAMED(PMEVTYPER9_EL0,  x)
#define BM_WRITE_PMEVTYPER10(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER10_EL0, x)
#define BM_WRITE_PMEVTYPER11(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER11_EL0, x)
#define BM_WRITE_PMEVTYPER12(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER12_EL0, x)
#define BM_WRITE_PMEVTYPER13(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER13_EL0, x)
#define BM_WRITE_PMEVTYPER14(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER14_EL0, x)
#define BM_WRITE_PMEVTYPER15(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER15_EL0, x)

#define BM_WRITE_PMEVTYPER16(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER16_EL0, x)
#define BM_WRITE_PMEVTYPER17(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER17_EL0, x)
#define BM_WRITE_PMEVTYPER18(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER18_EL0, x)
#define BM_WRITE_PMEVTYPER19(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER19_EL0, x)
#define BM_WRITE_PMEVTYPER20(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER20_EL0, x)
#define BM_WRITE_PMEVTYPER21(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER21_EL0, x)
#define BM_WRITE_PMEVTYPER22(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER22_EL0, x)
#define BM_WRITE_PMEVTYPER23(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER23_EL0, x)

#define BM_WRITE_PMEVTYPER24(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER24_EL0, x)
#define BM_WRITE_PMEVTYPER25(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER25_EL0, x)
#define BM_WRITE_PMEVTYPER26(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER26_EL0, x)
#define BM_WRITE_PMEVTYPER27(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER27_EL0, x)
#define BM_WRITE_PMEVTYPER28(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER28_EL0, x)
#define BM_WRITE_PMEVTYPER29(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER29_EL0, x)
#define BM_WRITE_PMEVTYPER30(x)  BM_WRITE_SYS_REG_NAMED(PMEVTYPER30_EL0, x)
#define BM_WRITE_PMCCFILTR(x)    BM_WRITE_SYS_REG_NAMED(PMCCFILTR_EL0,   x)

#define BM_READ_PMCCNTR_64(x)    BM_READ_PMCCNTR(x)
#define BM_WRITE_PMCCNTR_64(x)   BM_WRITE_PMCCNTR(x)

#endif

#define BM_READ_PMEVCNTR_LITERAL(n,x)     BM_READ_PMEVCNTR ##   n(x)
#define BM_READ_PMEVTYPER_LITERAL(n,x)    BM_READ_PMEVTYPER ##  n(x)
#define BM_WRITE_PMEVCNTR_LITERAL(n,x)    BM_WRITE_PMEVCNTR ##  n(x)
#define BM_WRITE_PMEVTYPER_LITERAL(n,x)   BM_WRITE_PMEVTYPER ## n(x)

#define BM_CALL_WITH_COUNTER_LITERAL_1(function, default_function, counter, arg1) \
    switch (counter)                        \
    {                                       \
        case  0: function( 0, arg1); break; \
        case  1: function( 1, arg1); break; \
        case  2: function( 2, arg1); break; \
        case  3: function( 3, arg1); break; \
        case  4: function( 4, arg1); break; \
        case  5: function( 5, arg1); break; \
        case  6: function( 6, arg1); break; \
        case  7: function( 7, arg1); break; \
        case  8: function( 8, arg1); break; \
        case  9: function( 9, arg1); break; \
        case 10: function(10, arg1); break; \
        case 11: function(11, arg1); break; \
        case 12: function(12, arg1); break; \
        case 13: function(13, arg1); break; \
        case 14: function(14, arg1); break; \
        case 15: function(15, arg1); break; \
        case 16: function(16, arg1); break; \
        case 17: function(17, arg1); break; \
        case 18: function(18, arg1); break; \
        case 19: function(19, arg1); break; \
        case 20: function(20, arg1); break; \
        case 21: function(21, arg1); break; \
        case 22: function(22, arg1); break; \
        case 23: function(23, arg1); break; \
        case 24: function(24, arg1); break; \
        case 25: function(25, arg1); break; \
        case 26: function(26, arg1); break; \
        case 27: function(27, arg1); break; \
        case 28: function(28, arg1); break; \
        case 29: function(29, arg1); break; \
        case 30: function(30, arg1); break; \
        default: default_function(arg1);    \
    }

#define BM_DO_NOTHING_1(x)        (void)(x)
#define BM_SET_TO_ZERO_1(x)       (x) = 0

#define BM_READ_PMEVCNTR(n,   x)   BM_CALL_WITH_COUNTER_LITERAL_1(BM_READ_PMEVCNTR_LITERAL,   BM_SET_TO_ZERO_1, n, x)
#define BM_READ_PMEVTYPER(n,  x)   BM_CALL_WITH_COUNTER_LITERAL_1(BM_READ_PMEVTYPER_LITERAL,  BM_SET_TO_ZERO_1, n, x)
#define BM_WRITE_PMEVCNTR(n,  x)   BM_CALL_WITH_COUNTER_LITERAL_1(BM_WRITE_PMEVCNTR_LITERAL,  BM_DO_NOTHING_1,  n, x)
#define BM_WRITE_PMEVTYPER(n, x)   BM_CALL_WITH_COUNTER_LITERAL_1(BM_WRITE_PMEVTYPER_LITERAL, BM_DO_NOTHING_1,  n, x)

#endif /* BM_PMU_AT_LEAST_V3 */

#if BM_PMU_AT_LEAST_V3_1

#if BM_ARM_32BIT_STATE

#define BM_READ_PMCEID2(x)    /* PMCEID0_EL0[63:32] */ BM_READ_SYS_REG(0,  9, 14, 4, x)
#define BM_READ_PMCEID3(x)    /* PMCEID1_EL0[63:32] */ BM_READ_SYS_REG(0,  9, 14, 5, x)

#define BM_READ_PMCEID0_64(x) /* PMCEID0_EL0        */       \
    do {                                                     \
        bm_uint32 _bm_pmceid0;                               \
        bm_uint32 _bm_pmceid2;                               \
        BM_READ_PMCEID0(_bm_pmceid0);                        \
        BM_READ_PMCEID2(_bm_pmceid2);                        \
        x = (((bm_uint64) _bm_pmceid2) << 32) | _bm_pmceid0; \
    } while (0)
#define BM_READ_PMCEID1_64(x) /* PMCEID1_EL0        */       \
    do {                                                     \
        bm_uint32 _bm_pmceid1;                               \
        bm_uint32 _bm_pmceid3;                               \
        BM_READ_PMCEID1(_bm_pmceid1);                        \
        BM_READ_PMCEID3(_bm_pmceid3);                        \
        x = (((bm_uint64) _bm_pmceid3) << 32) | _bm_pmceid1; \
    } while (0)

#elif BM_ARM_64BIT_STATE

#define BM_READ_PMCEID2(x)    /* PMCEID0_EL0[63:32] */ do { BM_READ_PMCEID0(x); x >> 32; } while (0)
#define BM_READ_PMCEID3(x)    /* PMCEID1_EL0[63:32] */ do { BM_READ_PMCEID1(x); x >> 32; } while (0)

#define BM_READ_PMCEID0_64(x) /* PMCEID0_EL0        */ BM_READ_PMCEID0(x)
#define BM_READ_PMCEID1_64(x) /* PMCEID1_EL0        */ BM_READ_PMCEID1(x)

#endif

#endif /* BM_PMU_AT_LEAST_V3_1 */

/** @} */

#endif /* INCLUDE_BARMAN_PMU */
