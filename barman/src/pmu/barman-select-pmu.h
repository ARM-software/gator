/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_PMU_SELECT_PMU
#define INCLUDE_BARMAN_PMU_SELECT_PMU

#include "barman-types.h"
#include "barman-config.h"
#include "pmu/barman-arm-pmu.h"
#include "pmu/barman-arm-dwt.h"

/* Select the appropriate PMU device */
/** @{ */
#if BM_CONFIG_USER_SUPPLIED_PMU_DRIVER
#   define barman_pmu_init(ne, et)          barman_ext_init(ne, et)
#   define barman_pmu_start()               barman_ext_start()
#   define barman_pmu_stop()                barman_ext_stop()
#   define barman_pmu_read_counter(n)       barman_ext_read_counter(n)
#   define barman_midr()                    barman_ext_midr()
#   define barman_mpidr()                   barman_ext_mpidr()
#elif BM_ARM_ARCH_PROFILE_IS_AR && BM_ARM_TARGET_ARCH >= 7
#   define BM_MAX_PMU_COUNTERS              BM_ARM_PMU_MAX_PMU_COUNTERS
#   define BM_PMU_INVALID_COUNTER_VALUE     BM_ARM_PMU_INVALID_COUNTER_VALUE
#   define BM_PMU_HAS_FIXED_CYCLE_COUNTER   1
#   define BM_PMU_CYCLE_COUNTER_ID          0
#   define BM_PMU_CYCLE_COUNTER_TYPE        ~0u
#   define barman_pmu_init(ne, et)          barman_arm_pmu_init(BM_TRUE, BM_FALSE, BM_PMU_HAS_FIXED_CYCLE_COUNTER, ne, et)
#   define barman_pmu_start()               barman_arm_pmu_start()
#   define barman_pmu_stop()                barman_arm_pmu_stop()
#   define barman_pmu_read_counter(n)       barman_arm_pmu_read_counter(n)
#elif BM_ARM_ARCH_PROFILE == 'M' && BM_ARM_TARGET_ARCH >= 7
#   define BM_MAX_PMU_COUNTERS              6
#   define BM_PMU_INVALID_COUNTER_VALUE     0
#   define barman_pmu_init(ne, et)          barman_arm_dwt_init(BM_TRUE, BM_TRUE, ne, et, BM_CONFIG_DWT_SAMPLE_PERIOD)
#   define barman_pmu_start()               barman_arm_dwt_start()
#   define barman_pmu_stop()                barman_arm_dwt_stop()
#   define barman_pmu_read_counter(n)       0
#else
#   pragma message ("WARNING: PMU driver not known")
#   define BM_MAX_PMU_COUNTERS              1
#   define BM_PMU_INVALID_COUNTER_VALUE     0
#   define BM_PMU_HAS_FIXED_CYCLE_COUNTER   1
#   define BM_PMU_CYCLE_COUNTER_ID          0
#   define BM_PMU_CYCLE_COUNTER_TYPE        0
#   define barman_pmu_init(ne, et)          0
#   define barman_pmu_start()
#   define barman_pmu_stop()
#   define barman_pmu_read_counter(n)       BM_PMU_INVALID_COUNTER_VALUE
#endif
/** @} */

#endif /* INCLUDE_BARMAN_PMU_SELECT_PMU */
