/* Copyright (C) 2017-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_ITM
#define INCLUDE_BARMAN_ITM

#include "barman-types.h"

struct bm_datastore_config_itm
{
    /** Base address of the ITM registers */
    void * registers;
};

/**
 * Initialize the ITM
 *
 * @param config    ITM run-time configuration
 * @return          True if successful
 */
bm_bool barman_itm_init(struct bm_datastore_config_itm config);

/**
 * Write as ITM frame
 *
 * @param data    Data to write in the frame
 * @param length  Length of the frame
 * @param channel Channel to write the frame on
 * @param flush   Whether to flush the channel after writing the frame (costs a 16 bit packet)
 */
void barman_itm_write_frame(const bm_uint8 * data, bm_uintptr length, bm_uint16 channel, bm_bool flush);

#endif /* INCLUDE_BARMAN_DATA_ITM */
