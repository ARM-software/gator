/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_EXTERNAL_DEPENDENCIES
#define INCLUDE_BARMAN_EXTERNAL_DEPENDENCIES

#include "barman-types-public.h"

/**
 * @defgroup    bm_external_api User supplied external functions
 * @details     Defines the functions that the user must implement to support
 *              the barman agent.
 * @{
 */

/**
 * @brief   Reads the current sample timestamp value
 * @details Must provide some timestamp value for the time at the point of the
 *          call. The timer must provide monotonically incrementing value since
 *          some implementation defined start point. The counter must not
 *          overflow during the period that it is used. The counter is in arbitrary units
 *          and the mechanism for converting those units to nanoseconds is described as part of the
 *          protocol data header.
 * @return  The timestamp value in arbitrary units.
 */
extern bm_uint64 barman_ext_get_timestamp(void);

/**
 * @brief   Disables interrupts on the local processor only.
 *          Used to allow atomic accesses to certain resources (e.g. PMU counters)
 * @return  The current interrupt enablement status value (which must be preserved and passed
 *          to barman_ext_enable_interrupts_local to restore the previous state)
 * @note    A weak implementation of this function is provided that modifies DAIF on AArch64, or
 *          CPSR on AArch32
 */
extern bm_uintptr barman_ext_disable_interrupts_local(void);

/**
 * @brief   Enables interrupts on the local processor only.
 * @param   previous_state  The value that was previously returned from barman_ext_disable_interrupts_local
 * @note    A weak implementation of this function is provided that modifies DAIF on AArch64, or
 *          CPSR on AArch32
 */
extern void barman_ext_enable_interrupts_local(bm_uintptr previous_state);

/**
 * @brief   Given the MPIDR register, returns a unique processor number.
 * @details The implementation must return a value between `[0, N)` where `N` is the maximum number of processors in the system.
 *          For any valid permutation of the arguments a unique value must be returned, which must not change between successive calls to this function for the
 *          same argument values.
 *
 *          @code
 *
 *          //
 *          // Example implementation where processors are arranged as follows:
 *          //
 *          // aff2 | aff1 | aff0 | cpuno
 *          // -----+------+------+------
 *          //    0 |    0 |    0 |     0
 *          //    0 |    0 |    1 |     1
 *          //    0 |    0 |    2 |     2
 *          //    0 |    0 |    3 |     3
 *          //    0 |    1 |    0 |     4
 *          //    0 |    1 |    1 |     5
 *          //
 *          bm_uint32 barman_ext_map_multiprocessor_affinity_to_core_no(bm_uintptr mpidr)
 *          {
 *              return (mpidr & 0x03) + ((mpidr >> 6) & 0x4);
 *          }
 *
 *          @endcode
 *
 * @param   mpidr   The value of the MPIDR register
 * @return  The processor number
 * @note    This function need only be defined when BM_CONFIG_MAX_CORES > 1
 */
extern bm_uint32 barman_ext_map_multiprocessor_affinity_to_core_no(bm_uintptr mpidr);

/**
 * @brief   Given the MPIDR register, return the appropriate cluster number.
 * @details Cluster IDs should be numbered `[0, N)` where `N` is the number of clusters in the system.
 *
 *          @code
 *
 *          //
 *          // Example implementation which is compatible with the example implementation of barman_ext_map_multiprocessor_affinity_to_core_no given
 *          // above.
 *          //
 *          bm_uint32 barman_ext_map_multiprocessor_affinity_to_cluster_no(bm_uintptr mpidr)
 *          {
 *              return ((mpidr >> 8) & 0x1);
 *          }
 *
 *          @endcode
 * @param   mpidr   The value of the MPIDR register
 * @return  The cluster number
 * @note    This function need only be defined when BM_CONFIG_MAX_CORES > 1
 */
extern bm_uint32 barman_ext_map_multiprocessor_affinity_to_cluster_no(bm_uintptr mpidr);

#if BM_CONFIG_MAX_TASK_INFOS > 0

/** @brief  Task ID type */
typedef bm_uint32 bm_task_id_t;

/**
 * @return  Return the current task ID.
 */
extern bm_task_id_t barman_ext_get_current_task_id(void);

#endif

#if BM_CONFIG_USER_SUPPLIED_PMU_DRIVER
# ifndef BM_MAX_PMU_COUNTERS
#   error "BM_MAX_PMU_COUNTERS is not defined, but BM_CONFIG_USER_SUPPLIED_PMU_DRIVER is true"
# endif
# ifndef BM_PMU_INVALID_COUNTER_VALUE
#   error "BM_PMU_INVALID_COUNTER_VALUE is not defined, but BM_CONFIG_USER_SUPPLIED_PMU_DRIVER is true"
# endif
# ifndef BM_PMU_HAS_FIXED_CYCLE_COUNTER
#   error "BM_PMU_HAS_FIXED_CYCLE_COUNTER is not defined, but BM_CONFIG_USER_SUPPLIED_PMU_DRIVER is true"
# elif BM_PMU_HAS_FIXED_CYCLE_COUNTER
#   ifndef BM_PMU_CYCLE_COUNTER_ID
#     error "BM_PMU_CYCLE_COUNTER_ID is not defined, but BM_PMU_HAS_FIXED_CYCLE_COUNTER is true"
#   endif
#   ifndef BM_PMU_CYCLE_COUNTER_TYPE
#     error "BM_PMU_CYCLE_COUNTER_TYPE is not defined, but BM_PMU_HAS_FIXED_CYCLE_COUNTER is true"
#   endif
# endif

/**
 * @brief   Initialize the PMU on the current core.
 * @details This function programs the PMU hardware on the current core. The
 *          function must be called once on each core where we are interested in
 *          reading the PMU.
 * @param   n_event_types
 *      The number of additional configurable events to enable
 * @param   event_types
 *      An array of length `n_event_types` containing the event types for the
 *      additional events to enable
 * @return  The number of events that were enabled which will be whichever is
 *      lower from `n_event_types`, or the maximum number of events supported
 *      by the PMU.
 */
BM_NONNULL((2))
extern bm_uint32 barman_ext_init(bm_uint32 n_event_types, const bm_uint32 * event_types);

/**
 * @brief   Start recording events in the PMU
 */
extern void barman_ext_start(void);

/**
 * @brief   Stop recording events in the PMU
 */
extern void barman_ext_stop(void);

/**
 * @brief   Read the value of a PMU counter
 * @param   counter_no  The counter number
 * @return  The value of the counter, or {@link BM_PMU_INVALID_COUNTER_VALUE}
 *          if the counter was not read. Caller should detect this and skip the counter.
 */
extern bm_uint64 barman_ext_read_counter(bm_uint32 counter_no);

#endif

/** @} */

#endif /* INCLUDE_BARMAN_EXTERNAL_DEPENDENCIES */
