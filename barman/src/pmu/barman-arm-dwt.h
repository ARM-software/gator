/* Copyright (C) 2017-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef INCLUDE_BARMAN_ARM_DWT
#define INCLUDE_BARMAN_ARM_DWT

#include "barman-types.h"


/*
 * @brief   Initialize the Arm DWT on the current core.
 * @details This function programs the DWT hardware on the current core. The
 *          function must be called once on each core where we want the DWT to
 *          output counter overflow packets.
 * @param   enable_pc_sampling          Enable PC sampling
 * @param   enable_exception_tracing    Enable exception tracing
 * @param   n_event_types   The number of additional configurable events to enable
 * @param   event_types     An array of length `n_event_types` containing the event types for the
 *                          additional events to enable
 * @param   cycle_counter_overflow  Number of cycles per PC sample or cycle overflow event.
 *                                  Valid values are 64 * i or 1024 * i where i is between
 *                                  1 and 16 inclusive. Other values will be rounded down or up to 64.
 * @return  0 if successful, -1 otherwise
 */
BM_NONNULL((4))
bm_int32 barman_arm_dwt_init(bm_bool enable_pc_sampling,
                             bm_bool enable_exception_tracing,
                             bm_uint32 n_event_types, const bm_uint32 * event_types,
                             bm_uint32 cycle_counter_overflow);

/**
 * @brief   Start recording events in the DWT
 */
void barman_arm_dwt_start(void);

/**
 * @brief   Stop recording events in the DWT
 */
void barman_arm_dwt_stop(void);

#endif /* INCLUDE_BARMAN_ARM_DWT */

