/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_DATA_STORE
#define INCLUDE_BARMAN_DATA_STORE

#include "barman-types.h"
#include "barman-atomics.h"

/**
 * @defgroup    bm_data_store   Data Store: Interface
 * @brief       The data storage interface defines a mechanism for writing arbitrary sized blocks of binary data.
 * @details     The interface is designed to allow zero-copy where possible. Users must get a block using
 *              {@link barman_ext_datastore_get_block}, write any data to it, then commit the block using
 *              {@link barman_ext_datastore_commit_block}.
 * @note        There is no mechanism to free a block, or mark one as invalid so if it is possible to fail between the
 *              call to {@link barman_ext_datastore_get_block} and {@link barman_ext_datastore_commit_block} it is the
 *              responsibility of the data protocol to handle that.
 * @note        The data storage layer is not responsible for any form of encoding it will simply push blocks of bytes
 *              into whereever they are stored. It is the responsibility of the protocol to ensure the data is
 *              decodable.
 *              _NB_ however, the data store will frame in-memory blocks with a {@link bm_datastore_block_length} length
 *              prefix. The protocol should not encode the length of the block into the binary data it writes.
 * @note        The user should not call {@link barman_ext_datastore_get_block} more than once for the same value of `core`
 *              before calling {@link barman_ext_datastore_commit_block} (i.e. the calls should come in pairs for any
 *              particular value of `core`). It is *undefined behaviour* to do otherwise. Implementations should take
 *              into account that it is valid to call {@link barman_ext_datastore_commit_block} for different values of `core`
 *              concurrently.
 *              If {@link barman_ext_datastore_get_block} returns null, there is no requirement to call
 *              {@link barman_ext_datastore_commit_block}.
 * @note        The buffer is allowed to return a block of memory that longer than the length requested in get_block
 *              for alignment or any other reason.
 * @{ */

/** Header length */
typedef bm_atomic_uint64    bm_datastore_header_length;

/** Data block length. This value may be marked with a padding in the MSB to indicate the block is padding rather than data */
typedef bm_atomic_uintptr   bm_datastore_block_length;

/** The length of a block encodes a marker in the MSB that says whether the block is valid or padding */
#define BM_DATASTORE_BLOCK_PADDING_BIT          (1ul << (sizeof(bm_datastore_block_length) * 8 - 1))
/** Extract the actual length value from the encoded block length */
#define BM_DATASTORE_GET_LENGTH_VALUE(v)        ((v) & ~BM_DATASTORE_BLOCK_PADDING_BIT)
/** Test if is padding block */
#define BM_DATASTORE_IS_PADDING_BLOCK(v)        (((v) & BM_DATASTORE_BLOCK_PADDING_BIT) != 0)
/** Encode block length */
#define BM_DATASTORE_ENCODE_PADDING_BLOCK(v, p) (BM_DATASTORE_GET_LENGTH_VALUE(v) | ((p) ? BM_DATASTORE_BLOCK_PADDING_BIT : 0))

/**
 * @brief   Structure passed to most in-memory data stores that forms part of the protocol header and contains
 *          data about the layout of the in-memory data.
 * @note    It is *undefined behaviour* if some external entity modifies any of the values in the header struct
 *          whilst they may be used by a datastore.
 */
struct bm_datastore_header_data
{
    bm_datastore_header_length buffer_length;   /**< The length of the buffer */
    bm_datastore_header_length write_offset;    /**< The current write offset; points to the first unwritten byte within the buffer.
                                                     For ring buffers this is the first byte past the end of the ring */
    bm_datastore_header_length read_offset;     /**< The current read offset. For ring buffers this is the start of the ring */
    bm_datastore_header_length total_written;   /**< Total number of bytes consumed; always increments */
    bm_uint8 * base_pointer;                    /**< The base address of the buffer */
};

#if BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_USER_SUPPLIED

/**
 * @brief   Initialize the data store
 * @param   header_data              A {@link struct bm_datastore_header_data} * header_data for in-memory data stores
 * @param   datastore_config         A void * that can be used to pass anything
 * @return  BM_TRUE if the data store initialized properly, BM_FALSE otherwise
 */
extern bm_bool barman_ext_datastore_initialize(
#if BM_CONFIG_DATASTORE_USER_SUPPLIED_IS_IN_MEMORY
    struct bm_datastore_header_data * header_data);
#else
    void * datastore_config);
#endif

/**
 * @brief   Get a pointer to a block of memory of `length` bytes which can be written to.
 * @details Where it is significant to the underlying data store, the value of `core` may be used to select some
 *          appropriate internal buffer or other data structure.
 * @param   core    The core number.
 * @param   length  The length of the data to write
 * @return  A pointer to the block of memory that may be written to, or null if the buffer is full / unable to write.
 * @note    It is *undefined behaviour* to write more that `length` bytes of data to the block.
 * @ingroup bm_external_api
 */
extern bm_uint8 * barman_ext_datastore_get_block(bm_uint32 core, bm_datastore_block_length length);

/**
 * @brief   Commit a completed block of memory.
 * @param   core            The core number.
 * @param   block_pointer   The non-null value previously returned by the *last* call to {@link barman_ext_datastore_get_block}
 *                          with the matching value of argument `core`.
 * @note    It is *undefined behaviour* to call this function with a value that does not match that which was returned
 *          by the *last* call to {@link barman_ext_datastore_get_block} with the matching value of argument `core`.
 * @ingroup bm_external_api
 */
BM_NONNULL((2))
extern void barman_ext_datastore_commit_block(bm_uint32 core, bm_uint8 * block_pointer);

/**
 * @brief   Close the data store.
 * @details Marks the data store as closed. No subsequent writes should complete.
 *          No calls to {@link barman_ext_datastore_commit_block} should complete.
 * @note    It is *undefined behaviour* to call {@link barman_ext_datastore_get_block} before the close, with the matching
 *          {@link barman_ext_datastore_commit_block} afterwards.
 * @ingroup bm_external_api
 */
extern void barman_ext_datastore_close(void);

/**
 * @brief   The contents of the protocol header have been updated
 * @details The data store should store / update / transmit the data that makes up the header.
 * @param   timestamp   The timestamp of the change
 * @param   header      The address of the data
 * @param   length      The length of the data
 */
BM_NONNULL((2))
extern void barman_ext_datastore_notify_header_updated(bm_uint64 timestamp, const void * header, bm_uintptr length);

#endif

/** @} */

#endif /* INCLUDE_BARMAN_DATA_STORE */
