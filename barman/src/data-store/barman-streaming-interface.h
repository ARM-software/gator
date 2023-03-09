/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_DATA_STORE_STREAMING_INTERFACE
#define INCLUDE_BARMAN_DATA_STORE_STREAMING_INTERFACE

#include "data-store/barman-data-store.h"
#include "data-store/barman-data-store-types.h"

/**
 * @defgroup    bm_data_store_streaming_interface     Data Store: A streaming datastore
 * @brief       Streams data over some multi-channel interface
 * @{ */

/**
 * @brief   Initialize the streaming datastore
 * @param   datastore_config A {@link bm_datastore_config}
 * @return  BM_TRUE if initialized successfully, BM_FALSE if not
 * @note    If this function is called multiple times, it will fail unless the buffer was previously closed
 *
 */
bm_bool barman_streaming_interface_initialize(bm_datastore_config datastore_config);

/**
 * @brief   Get a pointer to a block of memory of `length` bytes which can be written to.
 * @see     barman_datastore_get_block
 */
bm_uint8 * barman_streaming_interface_get_block(bm_datastore_block_length length);

/**
 * @brief   Commit a completed block of memory.
 * @see     barman_datastore_commit_block
 */
BM_NONNULL((1)) void barman_streaming_interface_commit_block(bm_uint8 * block_pointer);

/**
 * @brief   Close the data store.
 * @see     barman_datastore_close
 */
void barman_streaming_interface_close(void);

/**
 * @brief   The contents of the protocol header have been updated
 * @details The data store should store / update / transmit the data that makes up the header.
 * @param   timestamp   The timestamp of the change
 * @param   header      The address of the data
 * @param   length      The length of the data
 */
BM_NONNULL((2))
void barman_streaming_interface_notify_header_updated(bm_uint64 timestamp, const void * header, bm_uintptr length);

/** @} */

#endif /* INCLUDE_BARMAN_DATA_STORE_STREAMING_INTERFACE */
