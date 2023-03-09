/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_DATA_STORE_CIRCULAR_RAM_BUFFER
#define INCLUDE_BARMAN_DATA_STORE_CIRCULAR_RAM_BUFFER

#include "data-store/barman-data-store.h"

/**
 * @defgroup    bm_data_store_circular_ram_buffer     Data Store: The circular RAM buffer data store
 * @brief       Stores data in a fixed length circular RAM buffer.
 * @{ */

/**
 * @brief   Initialize the circular ram buffer
 * @param   header_data The header object to store data into
 * @return  BM_TRUE if initialized successfully, BM_FALSE if not
 * @note    If this function is called multiple times, it will fail unless the buffer was previously closed
 *
 */
BM_NONNULL((1))
bm_bool barman_circular_ram_buffer_initialize(struct bm_datastore_header_data * header_data);

/**
 * @brief   Get a pointer to a block of memory of `length` bytes which can be written to.
 * @see     barman_datastore_get_block
 */
bm_uint8 * barman_circular_ram_buffer_get_block(bm_uint32 core, bm_datastore_block_length length);

/**
 * @brief   Commit a completed block of memory.
 * @see     barman_datastore_commit_block
 */
BM_NONNULL((2))
void barman_circular_ram_buffer_commit_block(bm_uint32 core, bm_uint8 * block_pointer);

/**
 * @brief   Close the data store.
 * @see     barman_datastore_close
 */
extern void barman_circular_ram_buffer_close(void);

/** @} */

#endif /* INCLUDE_BARMAN_DATA_STORE_CIRCULAR_RAM_BUFFER */
