/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_PROTOCOL
#define INCLUDE_BARMAN_PROTOCOL

#include "barman-custom-counter-definitions.h"
#include "barman-protocol-api.h"
#include "data-store/barman-data-store-types.h"

/**
 * @brief   Initialize the protocol and underlying data store
 * @param   datastore_config    A backend specific configuration struct
 * @param   target_name     The target device name
 * @param   clock_info      Information about the monotonic clock used for timestamps
 * @param   num_task_entries    The length of the array of task entries in `task_entries`.
 *                              If this value is greater than {@link BM_CONFIG_MAX_TASK_INFOS} then it will be truncated.
 * @param   task_entries    The task information descriptors. Can be NULL.
 * @param   num_mmap_entries    The length of the array of mmap entries in `mmap_entries`.
 *                              If this value is greater than {@link BM_CONFIG_MAX_MMAP_LAYOUT} then it will be truncated.
 * @param   mmap_entries    The mmap image layout descriptors. Can be NULL.
 * @param   timer_sample_rate   Timer based sampling rate; in Hz. Zero indicates no timer based sampling (assumes max 4GHz sample rate)
 * @return  BM_TRUE on success, BM_FALSE on failure
 * @note    If BM_CONFIG_MAX_TASK_INFOS <= 0, then `num_task_entries` and `task_entries` are not present.
 *          If BM_CONFIG_MAX_MMAP_LAYOUTS <= 0, then `num_mmap_entries` and `mmap_entries` are not present.
 */
BM_NONNULL((2, 3))
bm_bool barman_protocol_initialize(bm_datastore_config datastore_config,
                                   const char * target_name, const struct bm_protocol_clock_info * clock_info,
#if BM_CONFIG_MAX_TASK_INFOS > 0
                                   bm_uint32 num_task_entries, const struct bm_protocol_task_info * task_entries,
#endif
#if BM_CONFIG_MAX_MMAP_LAYOUTS > 0
                                   bm_uint32 num_mmap_entries, const struct bm_protocol_mmap_layout * mmap_entries,
#endif
                                   bm_uint32 timer_sample_rate
                                  );

/**
 * @brief   Store the PMU counters that were configured for a given PMU
 * @param   timestamp       The timestamp the settings were configured
 * @param   midr            The MIDR register value of the core
 * @param   mpidr           The MPIDR register value of the core
 * @param   core            The core number the settings were for
 * @param   num_counters    The number of counters (the length of `counter_types`)
 * @param   counter_types   The PMU event types that were configured.
 * @return  BM_TRUE on success, BM_FALSE on failure
 * @ingroup bm_protocol
 */
BM_NONNULL((6))
bm_bool barman_protocol_write_pmu_settings(bm_uint64 timestamp, bm_uint32 midr, bm_uintptr mpidr, bm_uint32 core, bm_uint32 num_counters, const bm_uint32 * counter_types);

/**
 * @brief   Store a sample record for a core
 * @param   timestamp       The timestamp the sample was recorded
 * @param   core            The core number the settings were for
 * @param   task_id         The task ID associated with the sample, or zero if not associated with any task
 * @param   pc              The program counter value to associate with the sample, or ignored if BM_NULL
 * @param   num_counters    The length of `counter_values`
 * @param   counter_values  The counter values recorded for each counter.
 * @param   num_custom_counters     The number of customer counter values (length of `custom_counter_ids` and `custom_counter_values`)
 * @param   custom_counter_ids      Array of custom counter ids where each id maps to the value in `custom_counter_values` at the same index
 * @param   custom_counter_values   Array of custom counter values
 * @return  BM_TRUE on success, BM_FALSE on failure
 * @note    It is expected that there is a 1:1 mapping from `counter_value` to `counter_type` that was specified in
 *          {@link barman_protocol_write_pmu_settings} such that `counter_values[n]` is the value for counter specified in `counter_types[n]` for any valid
 *          value of `n`.
 * @note    If BM_CONFIG_MAX_TASK_INFOS <= 0, then `task_id` is not present.
 * @ingroup bm_protocol
 */
BM_NONNULL((5 + (BM_CONFIG_MAX_TASK_INFOS > 0 ? 1 : 0), 7 + (BM_CONFIG_MAX_TASK_INFOS > 0 ? 1 : 0), 8 + (BM_CONFIG_MAX_TASK_INFOS > 0 ? 1 : 0)))
bm_bool barman_protocol_write_sample(bm_uint64 timestamp, bm_uint32 core,
#if BM_CONFIG_MAX_TASK_INFOS > 0
                                     bm_task_id_t task_id,
#endif
                                     const void * pc,
                                     bm_uint32 num_counters, const bm_uint64 * counter_values,
                                     bm_uint32 num_custom_counters, const bm_uint32 * custom_counter_ids, const bm_uint64 * custom_counter_values);

#if BM_CONFIG_MAX_TASK_INFOS > 0
/**
 * @brief   Store a task switch record for a core
 * @param   timestamp       The timestamp the settings were configured
 * @param   core            The core number the settings were for
 * @param   task_id         The new task ID
 * @param   reason          The reason for the task switch
 * @return  BM_TRUE on success, BM_FALSE on failure
 */
bm_bool barman_protocol_write_task_switch(bm_uint64 timestamp, bm_uint32 core, bm_task_id_t task_id, bm_uint8 reason);
#endif

#if BM_CONFIG_MIN_SAMPLE_PERIOD > 0
/**
 * @brief   Gets the minimum sample period
 * @return  bm_uint64 The minimum sample period in the same unit returned by {@link bm_ext_get_timestamp}
 */
bm_uint64 barman_protocol_get_minimum_sample_period(void);
#endif

#if BM_NUM_CUSTOM_COUNTERS > 0
/**
 * @brief   Store a custom counter value record
 * @param   counter_id  The index of the custom counter
 * @param   timestamp   The timestamp of the event
 * @param   core        The core number the sample was taken on
 * @param   value       The counter value
 */
bm_bool barman_protocol_write_per_core_custom_counter(bm_uint64 timestamp, bm_uint32 core,
#if BM_CONFIG_MAX_TASK_INFOS > 0
                                                      bm_task_id_t task_id,
#endif
                                                      bm_uint32 counter_id, bm_uint64 value);
#endif

/**
 * @brief   Write a WFI/WFE halting event record
 * @param   timestamp       The timestamp of the event
 * @param   core            The core number the sample was taken on
 * @param   entered_halt    True if the record represents the time the processor entered the halt state,
 *                          False if the record represents the time the processor exited the halt state
 * @return  BM_TRUE on success, BM_FALSE on failure
 */
bm_bool barman_protocol_write_halt_event(bm_uint64 timestamp, bm_uint32 core, bm_bool entered_halt);

/**
 * @brief   Write an annotation_record
 * @param   timestamp       The timestamp of the annotation
 * @param   core            The core (ignored by record but needed for datastore operation)
 * @param   task_id         The task ID associated with the annotation, or zero if not associated with any task
 * @param   type            The annotation type
 * @param   channel         The channel number
 * @param   group           The group number
 * @param   color           The color
 * @param   data_length     The length of the annotation data
 * @param   data            The annotation data
 * @return  BM_TRUE on success, BM_FALSE on failure
 */
bm_bool barman_protocol_write_annotation(bm_uint64 timestamp, bm_uint32 core,
#if BM_CONFIG_MAX_TASK_INFOS > 0
                                         bm_task_id_t task_id,
#endif
                                         bm_uint8 type, bm_uint32 channel, bm_uint32 group, bm_uint32 color, bm_uintptr data_length, const bm_uint8 * data);

/** @} */

#endif /* INCLUDE_BARMAN_PROTOCOL */
