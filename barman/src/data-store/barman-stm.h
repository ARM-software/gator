/* Copyright (C) 2017-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_STM
#define INCLUDE_BARMAN_STM

#include "barman-types.h"

/**
 * Datastore configuration for the STM backend
 */
struct bm_datastore_config_stm
{
    /** Base address of the STM configuration registers */
    void * configuration_registers;
    /** Base address of the STM extended stimulus ports */
    void * extended_stimulus_ports;
};

/**
 * Initialize the STM
 *
 * @param config    STM run-time configuration
 */
bm_bool barman_stm_init(struct bm_datastore_config_stm config);

/**
 * Write as STM frame
 *
 * @param data    Data to write in the frame
 * @param length  Length of the frame
 * @param channel Channel to write the frame on
 * @param flush   Whether to flush the channel after writing the frame (costs a flag packet)
 */
void barman_stm_write_frame(const bm_uint8 * data, bm_uintptr length, bm_uint16 channel, bm_bool flush);

#endif /* INCLUDE_BARMAN_DATA_STM */
