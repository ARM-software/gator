/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#include "data-store/barman-linear-ram-buffer.h"
#include "barman-config.h"
#include "barman-atomics.h"
#include "barman-cache.h"
#include "barman-intrinsics.h"

/* *********************************** */

/**
 * @brief   Defines the data
 * @ingroup bm_data_store_linear_ram_buffer
 */
struct barman_linear_ram_buffer_configuration
{
    /** Header data */
    struct bm_datastore_header_data * header_data;
    /** Current commited write offset for each core */
    bm_datastore_header_length commited_offset[BM_CONFIG_MAX_CORES];
    /** Current write offset */
    bm_datastore_header_length write_offset;
    /** Closed flag */
    bm_atomic_bool closed;
};

/** The configuration settings */
static struct barman_linear_ram_buffer_configuration barman_linear_ram_buffer_configuration = { BM_NULL, { 0 }, 0, BM_TRUE };

/** Marker to indicate that a call to get_block was made on a core but is not yet complete */
#define BM_CORE_IS_BUSY     (~0ul)

/* *********************************** */

/**
 * @brief   Align a block size to a multiple of `sizeof(bm_datastore_block_length)`
 * @param   length  the length to align
 * @return  The aligned length
 */
static BM_INLINE bm_datastore_block_length barman_linear_ram_buffer_align_block_size(bm_datastore_block_length length)
{
    return ((length + (sizeof(bm_datastore_block_length) - 1)) & ~((bm_datastore_block_length) (sizeof(bm_datastore_block_length) - 1)));
}

/* *********************************** */

bm_bool barman_linear_ram_buffer_initialize(struct bm_datastore_header_data * header_data)
{
    /* Can only change the settings if closed */
    if (!barman_atomic_cmp_ex_strong_value(&barman_linear_ram_buffer_configuration.closed, BM_TRUE, BM_FALSE)) {
        return BM_FALSE;
    }

    /* Update configuration */
    barman_linear_ram_buffer_configuration.header_data = header_data;
    barman_linear_ram_buffer_configuration.write_offset = 0;
    barman_linear_ram_buffer_configuration.header_data->read_offset = 0;
    barman_linear_ram_buffer_configuration.header_data->write_offset = 0;
    barman_linear_ram_buffer_configuration.header_data->total_written = 0;

    barman_dsb();

    return BM_TRUE;
}

bm_uint8 * barman_linear_ram_buffer_get_block(bm_uint32 core, bm_datastore_block_length user_length)
{
    const bm_datastore_block_length aligned_length = barman_linear_ram_buffer_align_block_size(user_length);
    const bm_datastore_block_length required_length = aligned_length + sizeof(bm_datastore_block_length);
    bm_datastore_header_length old_write_offset = barman_atomic_load(&barman_linear_ram_buffer_configuration.write_offset);
    bm_datastore_header_length new_write_offset;
    bm_uint8 * block_pointer;

    /* Check length */
    if (aligned_length <= 0) {
        return BM_NULL;
    }

    /* Check core number */
    if (core >= BM_CONFIG_MAX_CORES) {
        return BM_NULL;
    }

    /* Check not already closed */
    if (barman_atomic_load(&barman_linear_ram_buffer_configuration.closed)) {
        return BM_NULL;
    }

    /* check not already got uncommited block */
    if (!barman_atomic_cmp_ex_strong_value(&(barman_linear_ram_buffer_configuration.commited_offset[core]), 0, BM_CORE_IS_BUSY) != 0) {
        return BM_NULL;
    }

    /* Update the write offset atomically */
    do {
        /* NB: old_write_offset is modified by barman_atomic_cmp_ex_weak_pointer if it fails */
        new_write_offset = old_write_offset + required_length;
    } while (!barman_atomic_cmp_ex_weak_pointer(&barman_linear_ram_buffer_configuration.write_offset, &old_write_offset, new_write_offset));

    /* Validate the new write offset */
    if (new_write_offset > barman_linear_ram_buffer_configuration.header_data->buffer_length) {
        /* It is safe to subtract the length we previously added to revert the header since any subsequent calls to
         * get_block will have only incremented the write_offset futher past the end */
        barman_atomic_sub_fetch(&barman_linear_ram_buffer_configuration.write_offset, required_length);
        return BM_NULL;
    }

    /* Configure the result */
    block_pointer = (barman_linear_ram_buffer_configuration.header_data->base_pointer + old_write_offset);

    /* Write the length value, but mark the block invalid */
    *BM_ASSUME_ALIGNED_CAST(bm_datastore_block_length, block_pointer) = BM_DATASTORE_ENCODE_PADDING_BLOCK(aligned_length, BM_TRUE);

    /* Write the commit offset */
    barman_atomic_store(&(barman_linear_ram_buffer_configuration.commited_offset[core]), new_write_offset);

    return block_pointer + sizeof(bm_datastore_block_length);
}

void barman_linear_ram_buffer_commit_block(bm_uint32 core, bm_uint8 * user_pointer)
{
    /* Check core number */
    if (core >= BM_CONFIG_MAX_CORES) {
        return;
    }

    /* Check not already closed */
    else if (barman_atomic_load(&barman_linear_ram_buffer_configuration.closed)) {
        return;
    }

    /* Pre tests are valid */
    else {
        bm_uint8 * const block_pointer = user_pointer - sizeof(bm_datastore_block_length);
        bm_datastore_block_length * length_pointer = BM_ASSUME_ALIGNED_CAST(bm_datastore_block_length, block_pointer);
        const bm_datastore_header_length new_write_offset = barman_atomic_load(&(barman_linear_ram_buffer_configuration.commited_offset[core]));
        bm_uint8 * const buffer_end = (barman_linear_ram_buffer_configuration.header_data->base_pointer
                + barman_atomic_load(&barman_linear_ram_buffer_configuration.write_offset));
        bm_datastore_header_length old_write_offset = barman_atomic_load(&barman_linear_ram_buffer_configuration.header_data->write_offset);
        bm_datastore_block_length user_length;
        bm_datastore_block_length required_length;

        /* Check pointer in bounds */
        if (block_pointer < barman_linear_ram_buffer_configuration.header_data->base_pointer) {
            return;
        }

        user_length = BM_DATASTORE_GET_LENGTH_VALUE(*length_pointer);
        required_length = user_length + sizeof(bm_datastore_block_length);

        if ((user_length == 0) || ((block_pointer + required_length) > buffer_end)) {
            return;
        }

        /* Check has something to commit */
        if ((new_write_offset == 0) || (new_write_offset == BM_CORE_IS_BUSY)) {
            return;
        }

        /* Write the length value, now marked as valid */
        *length_pointer = BM_DATASTORE_ENCODE_PADDING_BLOCK(user_length, BM_FALSE);

        /* Clean the cache lines that contain the data */
        barman_cache_clean(block_pointer, required_length);

        /* increment total */
        barman_atomic_fetch_add(&(barman_linear_ram_buffer_configuration.header_data->total_written), required_length);

        /* Update the header write_offset, only if it is not already greater than the per core offset.
         * This ensures that if the user stops the core using the debugger to read the data, then the header has a
         * marginally better chance that the last entry in the buffer is not invalid data as compared to if we
         * updated the header write_offset in get_block. */
        do {
            /* NB: old_write_offset is modified by barman_atomic_cmp_ex_weak_pointer if it fails */
            /* Do not update if the write offset is already greater */
            if (old_write_offset >= new_write_offset) {
                break;
            }
        } while (!barman_atomic_cmp_ex_weak_pointer(&barman_linear_ram_buffer_configuration.header_data->write_offset, &old_write_offset, new_write_offset));

        /* cache clean the header */
        barman_cache_clean(barman_linear_ram_buffer_configuration.header_data, sizeof(*barman_linear_ram_buffer_configuration.header_data));

        /* Clear the commit offset */
        barman_atomic_store(&(barman_linear_ram_buffer_configuration.commited_offset[core]), 0);
    }
}

void barman_linear_ram_buffer_close(void)
{
    barman_atomic_store(&barman_linear_ram_buffer_configuration.closed, BM_TRUE);
}
