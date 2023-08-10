/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_API_PUBLIC
#define INCLUDE_BARMAN_API_PUBLIC

#include "barman-intrinsics-public.h"
#include "barman-protocol-api.h"
#include "barman-types-public.h"
#include "multicore/barman-multicore.h"

#if (BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_STREAMING_USER_SUPPLIED)
#include "data-store/barman-ext-streaming-backend.h"
#endif

/**
 * @defgroup    bm_public   Public functions
 * @{ */

#if BM_CONFIG_MAX_TASK_INFOS > 0

/**
 * @brief   Reason for a task switch
 */
enum bm_task_switch_reason
{
    /** Thread is preempted */
    BM_TASK_SWITCH_REASON_PREEMPTED = 0,
    /** Thread is blocked waiting (e.g. on IO) */
    BM_TASK_SWITCH_REASON_WAIT = 1
};

#endif

/**
 * @brief   Initialize barman
 * @param   buffer              Pointer to in memory buffer
 * @param   buffer_length       The length of the in memory buffer
 * @param   stm_configuration_registers       Base address of the STM configuration registers.
 *                                            Can be NULL if it will be initialized elsewhere, e.g., by the debugger
 * @param   stm_extended_stimulus_ports       Base address of the STM extended stimulus ports.
 * @param   itm_registers       Base address of the ITM registers.
 * @param   datastore_config    Pointer to configuration to pass to {@link barman_ext_datastore_initialize}
 * @param   target_name         The target device name
 * @param   clock_info          Information about the monotonic clock used for timestamps
 * @param   num_task_entries    The length of the array of task entries in `task_entries`.
 *                              If this value is greater than {@link BM_CONFIG_MAX_TASK_INFOS} then it will be truncated.
 * @param   task_entries        The task information descriptors. Can be NULL.
 * @param   num_mmap_entries    The length of the array of mmap entries in `mmap_entries`.
 *                              If this value is greater than {@link BM_CONFIG_MAX_MMAP_LAYOUT} then it will be truncated.
 * @param   mmap_entries        The mmap image layout descriptors. Can be NULL.
 * @param   timer_sample_rate   Timer based sampling rate; in Hz. Zero indicates no timer based sampling (assumes max 4GHz sample rate).
 *                              This value is informative only and is used for reporting the timer frequency in the Streamline UI.
 * @return  BM_TRUE on success, BM_FALSE on failure
 * @note    If BM_CONFIG_MAX_TASK_INFOS <= 0, then `num_task_entries` and `task_entries` are not present.
 *          If BM_CONFIG_MAX_MMAP_LAYOUTS <= 0, then `num_mmap_entries` and `mmap_entries` are not present.
 */
BM_PUBLIC_FUNCTION
#if (BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_LINEAR_RAM_BUFFER) || (BM_CONFIG_USE_DATASTORE ==  BM_CONFIG_USE_DATASTORE_CIRCULAR_RAM_BUFFER) || ((BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_USER_SUPPLIED) && BM_CONFIG_DATASTORE_USER_SUPPLIED_IS_IN_MEMORY)
bm_bool barman_initialize(bm_uint8 * buffer, bm_uintptr buffer_length,
#elif BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_STM
bm_bool barman_initialize_with_stm_interface(void * stm_configuration_registers, void * stm_extended_stimulus_ports,
#elif BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_ITM
#if BM_ARM_ARCH_PROFILE == 'M'
bm_bool barman_initialize_with_itm_interface(
#else
bm_bool barman_initialize_with_itm_interface(void * itm_registers,
#endif
#elif (BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_USER_SUPPLIED) || (BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_STREAMING_USER_SUPPLIED)
bm_bool barman_initialize_with_user_supplied(void * datastore_config,
#else
#error "BM_CONFIG_USE_DATASTORE is not set correctly"
#endif
                          const char * target_name, const struct bm_protocol_clock_info * clock_info,
#if BM_CONFIG_MAX_TASK_INFOS > 0
                          bm_uint32 num_task_entries, const struct bm_protocol_task_info * task_entries,
#endif
#if BM_CONFIG_MAX_MMAP_LAYOUTS > 0
                          bm_uint32 num_mmap_entries, const struct bm_protocol_mmap_layout * mmap_entries,
#endif
                          bm_uint32 timer_sample_rate)
    BM_PUBLIC_FUNCTION_BODY(BM_TRUE)

/**
 * @brief   Enable sampling. Should be called once all PMUs are enabled and the data store is configured
 */
BM_PUBLIC_FUNCTION
void barman_enable_sampling(void)
    BM_PUBLIC_FUNCTION_BODY_VOID

/**
 * @brief   Disable sampling. Disables sampling without reconfiguring the PMU. Sampling may be resumed by a call to {@link barman_enable_sampling}
 */
BM_PUBLIC_FUNCTION
void barman_disable_sampling(void)
    BM_PUBLIC_FUNCTION_BODY_VOID

/**
 * @brief   Reads the configured PMU counters for the current core and inserts them into the data store.
 *          May also insert a program counter record using the return address as the PC sample.
 * @param   sample_return_address   BM_TRUE to sample the return address as PC, BM_FALSE to ignore.
 * @note    The PC values are what is shown in the Call Paths view in Streamline. Without calling this with `sample_return_address == BM_TRUE`
 *          or `barman_sample_counters_with_program_counter` with `pc != BM_NULL`, the Call Paths view will be blank.
 * @note    This function would typically be called with `sample_return_address == BM_TRUE` from application code not doing periodic sampling.
 * @note    This function must be run on the core for the PMU that it intends to sample from, and it must not be migrated to another core
 *          for the duration of the call. This is necessary as it will need to program the per-core PMU registers.
 */
BM_PUBLIC_FUNCTION
void barman_sample_counters(bm_bool sample_return_address)
    BM_PUBLIC_FUNCTION_BODY_VOID

/**
 * @brief   Reads the configured PMU counters for the current core and inserts them into the data store.
 *          Inserts a program counter record using the supplied PC value.
 * @param   pc  The PC value to record. The PC entry is not inserted if (pc == BM_NULL)
 * @note    The PC values are what is shown in the Call Paths view in Streamline. Without calling this with `pc != BM_NULL`
 *          or `barman_sample_counters` with `sample_return_address == BM_TRUE`, the Call Paths view will be blank.
 * @note    This function would typically be called from a periodic interrupt handler with the exception return address as pc.
 * @note    This function must be run on the core for the PMU that it intends to sample from, and it must not be migrated to another core
 *          for the duration of the call. This is necessary as it will need to program the per-core PMU registers.
 */
BM_PUBLIC_FUNCTION
void barman_sample_counters_with_program_counter(const void * pc)
    BM_PUBLIC_FUNCTION_BODY_VOID

#if BM_CONFIG_MAX_TASK_INFOS > 0

/**
 * @brief   Record that a task switch has occurred.
 * @param   reason Reason for the task switch
 * @note    This must be called after the task switch has occurred
 *          such that {@link bm_ext_get_current_task} returns the task_id of the switched to task.
 */
BM_PUBLIC_FUNCTION
void barman_record_task_switch(enum bm_task_switch_reason reason)
    BM_PUBLIC_FUNCTION_BODY_VOID

#endif

/**
 * @brief   Wraps WFI instruction, sends events before and after the WFI to log the time in WFI.
 * @details This function is safe to use in place of the usual WFI asm instruction as it will degenerate to just WFI instruction when
 *          barman is disabled.
 */
BM_PUBLIC_FUNCTION
void barman_wfi(void)
    BM_PUBLIC_FUNCTION_BODY_STATEMENT(barman_wfi_intrinsic())

/**
 * @brief   Wraps WFE instruction, sends events before and after the WFE to log the time in WFE.
 * @details This function is safe to use in place of the usual WFI asm instruction as it will degenerate to just WFE instruction when
 *          barman is disabled.
 */
BM_PUBLIC_FUNCTION
void barman_wfe(void)
    BM_PUBLIC_FUNCTION_BODY_STATEMENT(barman_wfe_intrinsic())

/**
 * @brief   May be called before a WFI/WFE or other similar halting event to log entry into the paused state.
 * @details Can be used in situations where `barman_wfi()`/`barman_wfe()` is not suitable.
 * @note    Must be used in pair with `barman_after_idle()`
 * @note    Using `barman_wfi()`/`barman_wfe()` is preferred in most cases as it takes care of calling the before and after functions
 */
BM_PUBLIC_FUNCTION
void barman_before_idle(void)
    BM_PUBLIC_FUNCTION_BODY_VOID

/**
 * @brief   May be called after a WFI/WFE or other similar halting event to log exit from the paused state.
 * @details Can be used in situations where `barman_wfi()`/`barman_wfe()` is not suitable.
 * @note    Must be used in pair with `barman_before_idle()`
 * @note    Using `barman_wfi()`/`barman_wfe()` is preferred in most cases as it takes care of calling the before and after functions
 */
BM_PUBLIC_FUNCTION
void barman_after_idle(void)
    BM_PUBLIC_FUNCTION_BODY_VOID

/**
 * @defgroup  bm_annotation_colors   Color macros to use for annotations
 * @{
 *
 * @def       BM_ANNOTATE_COLOR_XXXX
 * @brief     Named annotation colors
 *
 * @def       BM_ANNOTATE_COLOR_CYCLIC
 * @brief     Annotation color that cycles through a predefined set
 *
 * @def       BM_ANNOTATE_COLOR_RGB(R, G, B)
 * @brief     Create an annotation color from its components
 * @param     R The red component from 0 to 255
 * @param     G The green component from 0 to 255
 * @param     B The blue component from 0 to 255
 */
#define BM_ANNOTATE_COLOR_RED              0x1bff0000
#define BM_ANNOTATE_COLOR_BLUE             0x1b0000ff
#define BM_ANNOTATE_COLOR_GREEN            0x1b00ff00
#define BM_ANNOTATE_COLOR_PURPLE           0x1bff00ff
#define BM_ANNOTATE_COLOR_YELLOW           0x1bffff00
#define BM_ANNOTATE_COLOR_CYAN             0x1b00ffff
#define BM_ANNOTATE_COLOR_WHITE            0x1bffffff
#define BM_ANNOTATE_COLOR_LTGRAY           0x1bbbbbbb
#define BM_ANNOTATE_COLOR_DKGRAY           0x1b555555
#define BM_ANNOTATE_COLOR_BLACK            0x1b000000

#define BM_ANNOTATE_COLOR_CYCLIC           0

#define BM_ANNOTATE_COLOR_RGB(R, G, B)     (0x1b << 24 | (((R) & 0xff) << 16) | (((G) & 0xff) << 8) | ((B) & 0xff))
/** @} */

/**
 * @brief   Adds a string annotation with a display color, and assigns it to a channel.
 * @param   channel  The channel number.
 * @param   color    The annotation color from {@link bm_annotation_colors}.
 * @param   text     The annotation text or null to end previous annotation.
 * @note    Annotation channels and groups are used to organize annotations within the threads and processes section of the Timeline view.
 *          Each annotation channel appears in its own row under the thread. Channels can also be grouped and displayed under a group name, using the `barman_annotate_name_group` function.
 */
BM_PUBLIC_FUNCTION
void barman_annotate_channel(bm_uint32 channel, bm_uint32 color, const char * string)
    BM_PUBLIC_FUNCTION_BODY_VOID

/**
 * @brief   Defines a channel and attaches it to an existing group.
 * @param   channel  The channel number.
 * @param   group    The group number.
 * @param   name     The name of the channel.
 * @note    The channel number must be unique within the task.
 */
BM_PUBLIC_FUNCTION
void barman_annotate_name_channel(bm_uint32 channel, bm_uint32 group, const char * name)
    BM_PUBLIC_FUNCTION_BODY_VOID

/**
 * @brief   Defines an annotation group.
 * @param   group    The group number.
 * @param   name     The name of the group.
 * @note    The group identifier, group, must be unique within the task.
 */
BM_PUBLIC_FUNCTION
void barman_annotate_name_group(bm_uint32 group, const char * name)
    BM_PUBLIC_FUNCTION_BODY_VOID

/**
 * @brief   Adds a bookmark with a string and a color to the Timeline and Log views.
 * @details The string is displayed in the Timeline view when you hover over the bookmark and in the Message column in the Log view.
 * @param   color    The marker color from {@link bm_annotation_colors}.
 * @param   text     The marker text or null for no text.
 */
BM_PUBLIC_FUNCTION
void barman_annotate_marker(bm_uint32 color, const char * text)
    BM_PUBLIC_FUNCTION_BODY_VOID

/** @} */

#endif /* INCLUDE_BARMAN_API_PUBLIC */
