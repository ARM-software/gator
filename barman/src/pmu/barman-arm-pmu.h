/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_ARM_PMU
#define INCLUDE_BARMAN_ARM_PMU

#include "barman-types.h"

/**
 * @defgroup    bm_arm_pmu    Arm PMU driver
 * @{ */

/** The maximum number of counters supported */
#define BM_ARM_PMU_MAX_PMU_COUNTERS      32

/** The value returned by {@link barman_arm_pmu_read_counter} if the counter was not read */
#define BM_ARM_PMU_INVALID_COUNTER_VALUE (~0ull)

/**
 * @brief   Initialize the ARMv7 PMU on the current core.
 * @details This function programs the PMU hardware on the current core. The
 *          function must be called once on each core where we are interested in
 *          reading the PMU.
 * @param   enable_pl0_access
 *      Enable reading the counter values from EL0. NB: Setting this bit will enable all registers to be accessible from
 *      user space, not just the counter read registers.
 * @param   enable_interrupts
 *      Enable PMU interrupts
 * @param   enable_cycle_counter
 *      Enable the 64-bit cycle counter
 * @param   n_event_types
 *      The number of additional configurable events to enable
 * @param   event_types
 *      An array of length `n_event_types` containing the event types for the
 *      additional events to enable
 * @return  The number of events that were enabled which will be whichever is
 *      lower from `n_event_types`, or the maximum number of events supported
 *      by the PMU. The number is increased by one if enable_cycle_counter was set.
 */
BM_NONNULL((5))
bm_int32 barman_arm_pmu_init(bm_bool enable_pl0_access, bm_bool enable_interrupts, bm_bool enable_cycle_counter, bm_uint32 n_event_types,
                                 const bm_uint32 * event_types);

/**
 * @brief   Start recording events in the PMU
 */
void barman_arm_pmu_start(void);

/**
 * @brief   Stop recording events in the PMU
 */
void barman_arm_pmu_stop(void);

/**
 * @brief   Read the value of a PMU counter
 * @param   counter_no
 *      The counter number, where 0 is the cycle counter and [1, N] are the user
 *      defined event counters as specified to {@link barman_arm_pmu_init} in
 *      `event_types` parameter.
 * @return  The value of the counter, or {@link BM_INVALID_COUNTER_VALUE}
 *          if the counter was not read. Caller should detect this and skip the counter.
 */
bm_uint64 barman_arm_pmu_read_counter(bm_uint32 counter_no);

/** @} */

#endif /* INCLUDE_BARMAN_PMU_API */
