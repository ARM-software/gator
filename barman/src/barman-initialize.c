/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#include "barman-api.h"
#include "barman-log.h"
#include "barman-protocol.h"
#if BM_ARM_ARCH_PROFILE == 'M'
#include "m-profile/barman-arch-constants.h"
#endif

/**
 * Do any generated initialization
 *
 * @return True if successful
 */
extern bm_bool barman_generated_initialize(void);

#if (BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_LINEAR_RAM_BUFFER) || (BM_CONFIG_USE_DATASTORE ==  BM_CONFIG_USE_DATASTORE_CIRCULAR_RAM_BUFFER) || ((BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_USER_SUPPLIED) && BM_DATASTORE_IS_IN_MEMORY)
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
{
#if BM_DATASTORE_IS_IN_MEMORY
    struct bm_datastore_config_in_memory datastore_config;

    if (buffer == BM_NULL) {
        BM_ERROR("buffer must not be null\n");
        return BM_FALSE;
    }
    datastore_config.buffer = buffer;
    datastore_config.buffer_length = buffer_length;

#elif BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_STM
    struct bm_datastore_config_stm datastore_config;

    if (stm_extended_stimulus_ports == BM_NULL) {
        BM_ERROR("stm_extended_stimulus_ports must not be null\n");
        return BM_FALSE;
    }
    datastore_config.configuration_registers = stm_configuration_registers;
    datastore_config.extended_stimulus_ports = stm_extended_stimulus_ports;


#elif BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_ITM
    struct bm_datastore_config_itm datastore_config;

#if BM_ARM_ARCH_PROFILE == 'M'
    datastore_config.registers = BM_ITM_BASE_ADDRESS;
#else
    if (itm_registers == BM_NULL) {
        BM_ERROR("itm_registers must not be null\n");
        return BM_FALSE;
    }
    datastore_config.registers = itm_registers;

#endif
#endif

    /* validate target_name */
    if (target_name == BM_NULL) {
        BM_ERROR("target_name must be provided\n");
        return BM_FALSE;
    }

    /* validate clock_info */
    if (clock_info == BM_NULL) {
        BM_ERROR("clock_info must be provided\n");
        return BM_FALSE;
    }


    if (!barman_protocol_initialize(datastore_config,
                                    target_name, clock_info,
#if BM_CONFIG_MAX_TASK_INFOS > 0
                                    num_task_entries, task_entries,
#endif
#if BM_CONFIG_MAX_MMAP_LAYOUTS > 0
                                    num_mmap_entries, mmap_entries,
#endif
                                    timer_sample_rate)) {
        return BM_FALSE;
    };

    return barman_generated_initialize();
}
