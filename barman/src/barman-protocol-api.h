/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_PROTOCOL_API
#define INCLUDE_BARMAN_PROTOCOL_API

#include "barman-types-public.h"
#include "barman-external-dependencies.h"

/**
 * @defgroup    bm_protocol     Protocol settings
 * @{ */

/**
 * @brief   Defines information about the monotonic clock used in the trace.
 * @details Timestamp information is stored in arbitrary units within samples.
 *          This reduces the overhead of making the trace by removing the need to
 *          transform the timestamp into nanoseconds at the point the sample is recorded.
 *          The host expects timestamps to be in nanoseconds.
 *          The arbitrary timestamp information is transformed to nanoseconds according to the following formula:
 *
 *          `bm_uint64 nanoseconds = (((timestamp - timestamp_base) * timestamp_multiplier) / timestamp_divisor;`
 *
 *          Therefore for a clock that already returns time in nanoseconds, `timestamp_multiplier` and
 *          `timestamp_divisor` should be configured as `1` and `1`.
 *          If the clock counts in microseconds then the multiplier and divisor should be set `1000` and `1`.
 *          If the clock counts at a rate of `n` Hz, then multiplier should be set `1000000000` and divisor as `n`.
 *
 */
struct bm_protocol_clock_info
{
    /** The base value of the timestamp such that the this value is zero in the trace */
    bm_uint64 timestamp_base;
    /** The clock rate ratio multiplier */
    bm_uint64 timestamp_multiplier;
    /** The clock rate ratio divisor */
    bm_uint64 timestamp_divisor;
    /** The unix timestamp base value such that a `timestamp_base` maps to `unix_base` unix time value. In nanoseconds */
    bm_uint64 unix_base_ns;
} BM_PACKED_TYPE;

#if BM_CONFIG_MAX_TASK_INFOS > 0
/**
 * @brief   A task information record.
 * @details Describes information about a unique task within the system
 */
struct bm_protocol_task_info
{
    /** The task id */
    bm_task_id_t task_id;
    /** The name of the task */
    const char * task_name;
};
#endif


#if BM_CONFIG_MAX_MMAP_LAYOUTS > 0
/**
 * @brief   A MMAP layout record.
 * @details Describes the position of some executable image (or section thereof) in memory allowing the host to map PC values to the appropriate
 *          executable image.
 */
struct bm_protocol_mmap_layout
{
#if BM_CONFIG_MAX_TASK_INFOS > 0
    /** The task ID to associate with the map */
    bm_task_id_t task_id;
#endif
    /** The base address of the image or image section */
    bm_uintptr base_address;
    /** The length of the image or image section */
    bm_uintptr length;
    /** The image section offset */
    bm_uintptr image_offset;
    /** The name of the image */
    const char * image_name;
};
#endif


#if BM_CONFIG_MAX_TASK_INFOS > 0

/**
 * @brief   Add a new task information record
 * @param   timestamp   The timestamp the record is inserted
 * @param   task_entry  The new task entry
 * @return  BM_TRUE on success, BM_FALSE on failure
 */
BM_PUBLIC_FUNCTION
BM_NONNULL((2))
bm_bool barman_add_task_record(bm_uint64 timestamp, const struct bm_protocol_task_info * task_entry)
    BM_PUBLIC_FUNCTION_BODY(BM_TRUE)

#endif /* BM_CONFIG_MAX_TASK_INFOS > 0 */

#if BM_CONFIG_MAX_MMAP_LAYOUTS > 0

/**
 * @brief   Add a new mmap information record
 * @param   timestamp   The timestamp the record is inserted
 * @param   mmap_entry  The new mmap entry
 * @return  BM_TRUE on success, BM_FALSE on failure
 */
BM_PUBLIC_FUNCTION
BM_NONNULL((2))
bm_bool barman_add_mmap_record(bm_uint64 timestamp, const struct bm_protocol_mmap_layout * mmap_entry)
    BM_PUBLIC_FUNCTION_BODY(BM_TRUE)

#endif /* BM_CONFIG_MAX_MMAP_LAYOUTS > 0 */

/** @} */

#endif /* INCLUDE_BARMAN_PROTOCOL_API */
