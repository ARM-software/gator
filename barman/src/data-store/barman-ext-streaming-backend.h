/* Copyright (C) 2017-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_EXT_STREAMING_BACKEND
#define INCLUDE_BARMAN_EXT_STREAMING_BACKEND

#include "data-store/barman-data-store-types.h"
#include "barman-types.h"

/**
 * Initialize the backend
 *
 * @param config    Pointer to some configuration data
 * @return          True if successful
 */
bm_bool barman_ext_streaming_backend_init(void * config);

/**
 * Write data as a frame
 *
 * @param data    Data to write in the frame
 * @param length  Length of the frame
 * @param channel Channel to write the frame on
 * @param flush   Whether to flush the channel after writing the frame (may have some overhead)
 */
void barman_ext_streaming_backend_write_frame(const bm_uint8 * data, bm_uintptr length, bm_uint16 channel, bm_bool flush);

/**
 * Shutdown the backend
 */
void barman_ext_streaming_backend_close(void);

/**
 * Get the channel bank
 *
 * If banked by core this is just the core number
 * If not banked this should always be 0
 *
 * @return The bank
 */
bm_uint32 barman_ext_streaming_backend_get_bank(void);

#endif /* INCLUDE_BARMAN_EXT_STREAMING_BACKEND */
