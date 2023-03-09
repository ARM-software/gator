/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file    data-store/barman-circular-ram-buffer.c
 * @brief   Implementation of a lock-free circular buffer
 * @details The buffer is implemented as a sort of list of arbitrary length blocks with `head_offset` indicating the offset to the base of the allocated data
 *          (the read offset), and `tail_offset` indicating the offset to the end of the allocated data (the write offset).
 *
 *          The real offsets into the buffer are `head_offset % buffer_length` and `tail_offset % buffer_length`; that is to say that the algorithm will only
 *          ever increment the offsets.
 *
 *          When a user requests a new block, the algorithm will first copy `tail_offset` into `reserved_tail_offset[core]`; this marks the current value of
 *          `tail_offset` as reserved for a given core. This is used to prevent other threads from freeing blocks beyond `tail_offset`; i.e. from trampling on
 *          data that is currently being used by a given core.
 *
 *          The algorithm will then ensure it has enough space to allocate the block (allowing for alignment in the case that the block would otherwise overlap
 *          the end of the buffer as each block returned must be contiguous). It will do this by freeing blocks from `head_offset` upto
 *          `MIN(reserved_tail_offset[0] ... reserved_tail_offset[MAX_CORES - 1], tail_offset)` (excluding any cores that are not reserving any data).
 *
 *          So long as enough space was freed it will then attempt to atomically CAS `tail_offset` with `tail_offset + required_length + alignment_length`. If
 *          the CAS fails, some other thread reserved the space first and the algorithm will restart. If the CAS succeeds it will fill in the appropriate parts
 *          of the block and return the pointer.
 *
 *          When the block is commited some time later, the algorith will update the header with the appropriate value for `write_offset` and will reset
 *          `reserved_tail_offset[core]` to some marker value (`BM_NO_RESERVED_TAIL`) indicating that it is not reserving any space. The algorithm is free to
 *          overwrite the data in the buffer previously reserved by the block at this point.
 *
 * @note    `barman_circular_ram_buffer_get_block` is free to fail if it cannot find enough free space for a block. This will usually happen if
 *          `MIN(reserved_tail_offset[0] ... reserved_tail_offset[MAX_CORES - 1], tail_offset)` is such a value that it cannot free enough space. The rate of
 *          failure is therefore a function of the number of cores accessing the buffer concurrently, the rate at which the blocks are requested and then
 *          commited, and the overall size of the buffer.
 *
 * @note    If a low priority thread were to successfully call `barman_circular_ram_buffer_get_block` and then subsequently be preempted and prevented from
 *          running before calling `barman_circular_ram_buffer_commit_block`, whilst other threads were using the buffer, the buffer would eventually reach a
 *          point where no more blocks could be allocated until the low priority thread is resumed. This situation will not lead to deadlock, but will instead
 *          cause all subsequent get requests to fail.
 */

#include "data-store/barman-circular-ram-buffer.h"
#include "barman-config.h"
#include "barman-atomics.h"
#include "barman-cache.h"
#include "barman-intrinsics.h"
#include "barman-memutils.h"
#include "multicore/barman-multicore.h"

/* *********************************** */

/**
 * @brief   Defines the data
 * @ingroup bm_data_store_circular_ram_buffer
 */
struct barman_circular_ram_buffer_configuration
{
    /** Header data */
    struct bm_datastore_header_data * header_data;
    /** Current commited write offset for each core */
    bm_atomic_uint64 reserved_tail_offset[BM_CONFIG_MAX_CORES];
    /** Buffer read offset */
    bm_atomic_uint64 head_offset;
    /** Buffer write offset */
    bm_atomic_uint64 tail_offset;
    /** Closed flag */
    bm_atomic_bool closed;
};

/** The configuration settings */
static struct barman_circular_ram_buffer_configuration barman_circular_ram_buffer_configuration = { BM_NULL, { 0 }, 0, 0, BM_TRUE };

/** Marker to indicate that no data is reserved */
#define BM_NO_RESERVED_TAIL     (~0ul)

/* *********************************** */

/**
 * @brief   Align a block size to a multiple of `sizeof(bm_datastore_block_length)`
 * @param   length  the length to align
 * @return  The aligned length
 */
static BM_INLINE bm_datastore_block_length barman_circular_ram_buffer_align_block_size(bm_datastore_block_length length)
{
    return ((length + (sizeof(bm_datastore_block_length) - 1)) & ~((bm_datastore_block_length) (sizeof(bm_datastore_block_length) - 1)));
}

/**
 * @brief   Find the lowest reserved tail value for all cores
 * @param   tail_offset     The current tail offset
 * @param   excluding_core  A core to exclude from the result, or BM_CONFIG_MAX_CORES to include all cores
 * @return  The lowest reserved tail value, or `tail_offset` if no tail values are reserved
 */
static BM_INLINE bm_datastore_header_length barman_circular_ram_buffer_get_reserved_tail(const bm_atomic_uint64 tail_offset, bm_uint32 excluding_core)
{
    bm_uint32 core;
    bm_atomic_uint64 result = tail_offset;

    for (core = 0; core < BM_CONFIG_MAX_CORES; ++core) {
        if (core != excluding_core) {
            const bm_atomic_uint64 offset = barman_atomic_load(&(barman_circular_ram_buffer_configuration.reserved_tail_offset[core]));

            if (offset != BM_NO_RESERVED_TAIL) {
                result = BM_MIN(result, offset);
            }
        }
    }

    return result;
}

/**
 * @brief   Free a single previously allocated block
 * @param   base_pointer    The base address of the buffer
 * @param   buffer_length   The length of the buffer
 * @param   head_offset_ptr [IN/OUT] Contains the current head_offset on entry, and will be updated to contain the new value of head_offset if it changes
 * @param   limit_offset    The limit offset whereby if the block were to move the head_offset past this point, the free fails.
 * @return  BM_TRUE if the block was freed, BM_FALSE if not
 */
BM_NONNULL((1, 3))
static bm_bool barman_circular_ram_buffer_free_block(bm_uint8 * const base_pointer, const bm_datastore_header_length buffer_length,
                                                     bm_atomic_uint64 * head_offset_ptr, const bm_atomic_uint64 limit_offset)
{
    const bm_datastore_header_length real_head_offset = *head_offset_ptr % buffer_length;
    bm_datastore_block_length * const block_pointer = BM_ASSUME_ALIGNED_CAST(bm_datastore_block_length, base_pointer + real_head_offset);

    /* read the length of the block */
    const bm_datastore_block_length user_length = BM_DATASTORE_GET_LENGTH_VALUE(*block_pointer);
    const bm_datastore_block_length block_length = user_length + sizeof(bm_datastore_block_length);

    /* validate we can free the block */
    const bm_atomic_uint64 new_head_offset = (block_length + *head_offset_ptr);
    if (new_head_offset > limit_offset) {
        return BM_FALSE;
    }

    /* try to free the block */
    if (barman_atomic_cmp_ex_strong_pointer(&barman_circular_ram_buffer_configuration.head_offset, head_offset_ptr, new_head_offset)) {
        *head_offset_ptr = new_head_offset;

        /* move the header pointer */
        barman_atomic_store(&barman_circular_ram_buffer_configuration.header_data->read_offset, *head_offset_ptr);
    }

    return BM_TRUE;
}

/**
 * @brief   Free all blocks between head_offset and tail_offset
 * @param   buffer_length   The length of the buffer
 * @param   base_pointer    The buffer base address
 * @param   tail_offset     The limit to free upto
 * @return  BM_TRUE if blocks were freed up to tail_offset, BM_FALSE on error
 */
BM_NONNULL((2))
static bm_bool barman_circular_ram_buffer_free_to_tail(const bm_datastore_header_length buffer_length, bm_uint8 * const base_pointer,
                                                       bm_atomic_uint64 tail_offset)
{
    const bm_atomic_uint64 reserved_tail_pointer = barman_circular_ram_buffer_get_reserved_tail(tail_offset, BM_CONFIG_MAX_CORES);

    if (reserved_tail_pointer < tail_offset) {
        return BM_FALSE;
    }
    else {
        bm_atomic_uint64 head_offset = barman_atomic_load(&barman_circular_ram_buffer_configuration.head_offset);

        /* loop freeing blocks until head_offset == tail_offset */
        while (head_offset < tail_offset) {
            if (!barman_circular_ram_buffer_free_block(base_pointer, buffer_length, &head_offset, tail_offset)) {
                return BM_FALSE;
            }
        }

        /* success */
        return BM_TRUE;
    }
}

/**
 * @brief   Free blocks until there is sufficient space
 * @param   buffer_length   The length of the buffer
 * @param   base_pointer    The buffer base address
 * @param   head_offset_out [OUT] Will contain the final value of head_offset
 * @param   tail_offset     The current value of tail_offset
 * @param   required_length The required amount of space to free
 * @return  BM_TRUE if blocks were freed up to enough free space, BM_FALSE on error
 */
BM_NONNULL((2, 3))
static bm_bool barman_circular_ram_buffer_ensure_free(const bm_datastore_header_length buffer_length, bm_uint8 * const base_pointer,
                                                      bm_atomic_uint64 * head_offset_out, bm_atomic_uint64 tail_offset,
                                                      const bm_datastore_block_length required_length)
{
    const bm_atomic_uint64 reserved_tail_offset = barman_circular_ram_buffer_get_reserved_tail(tail_offset, BM_CONFIG_MAX_CORES);

    bm_atomic_uint64 head_offset = barman_atomic_load(&barman_circular_ram_buffer_configuration.head_offset);

    /* loop freeing blocks until there is enough free space, or until we meet the reserved limit */
    while ((buffer_length - (tail_offset - head_offset)) < required_length) {
        if (!barman_circular_ram_buffer_free_block(base_pointer, buffer_length, &head_offset, reserved_tail_offset)) {
            return BM_FALSE;
        }
    }

    /* success */
    *head_offset_out = head_offset;
    return BM_TRUE;
}

/**
 * @brief   Commit the contents of some block, either a real block or an alignment block
 * @param   core            The core affected
 * @param   block_pointer   The data pointer
 * @param   length          The length of the data
 */
BM_NONNULL((2))
static void barman_circular_ram_buffer_write_commit(bm_uint32 core, bm_uint8 * block_pointer, bm_datastore_block_length length)
{
    bm_datastore_header_length old_write_offset = barman_atomic_load(&(barman_circular_ram_buffer_configuration.header_data->write_offset));
    bm_datastore_header_length new_write_offset;

    /* Clean the cache lines that contain the data */
    barman_cache_clean(block_pointer, length);

    /* increment total */
    barman_atomic_fetch_add(&(barman_circular_ram_buffer_configuration.header_data->total_written), length);

    /* Update the header write_offset */
    do {
        new_write_offset = barman_circular_ram_buffer_get_reserved_tail(barman_atomic_load(&barman_circular_ram_buffer_configuration.tail_offset), core);
    } while (!barman_atomic_cmp_ex_strong_pointer(&(barman_circular_ram_buffer_configuration.header_data->write_offset), &old_write_offset, new_write_offset));

    /* cache clean the header */
    barman_cache_clean(barman_circular_ram_buffer_configuration.header_data, sizeof(*barman_circular_ram_buffer_configuration.header_data));

    /* Clear the commit offset */
    barman_atomic_store(&(barman_circular_ram_buffer_configuration.reserved_tail_offset[core]), BM_NO_RESERVED_TAIL);
}

/* *********************************** */

bm_bool barman_circular_ram_buffer_initialize(struct bm_datastore_header_data * header_data)
{
    bm_uint32 i;

    /* Can only change the settings if closed */
    if (!barman_atomic_cmp_ex_strong_value(&barman_circular_ram_buffer_configuration.closed, BM_TRUE, BM_FALSE)) {
        return BM_FALSE;
    }

    /* Update configuration */
    barman_circular_ram_buffer_configuration.header_data = header_data;
    barman_circular_ram_buffer_configuration.head_offset = 0;
    barman_circular_ram_buffer_configuration.tail_offset = 0;
    barman_circular_ram_buffer_configuration.header_data->read_offset = 0;
    barman_circular_ram_buffer_configuration.header_data->write_offset = 0;
    barman_circular_ram_buffer_configuration.header_data->total_written = 0;
    for (i = 0; i < BM_CONFIG_MAX_CORES; ++i) {
        barman_circular_ram_buffer_configuration.reserved_tail_offset[i] = BM_NO_RESERVED_TAIL;
    }

    /* make sure length is aligned to multiple of sizeof(bm_datastore_block_length) */
    barman_circular_ram_buffer_configuration.header_data->buffer_length &= ~((bm_datastore_header_length) (sizeof(bm_datastore_block_length) - 1));

    barman_dsb();

    return BM_TRUE;
}

bm_uint8 * barman_circular_ram_buffer_get_block(bm_uint32 core, bm_datastore_block_length user_length)
{
    const bm_datastore_block_length aligned_length = barman_circular_ram_buffer_align_block_size(user_length);
    const bm_datastore_block_length required_length = aligned_length + sizeof(bm_datastore_block_length);
    bm_datastore_header_length buffer_length;
    bm_atomic_uint64 tail_offset;
    bm_uint8 * base_pointer;
    bm_uint8 * block_pointer = BM_NULL;

    /* Check length */
    if (aligned_length <= 0) {
        return BM_NULL;
    }

    /* Check core number */
    if (core >= BM_CONFIG_MAX_CORES) {
        return BM_NULL;
    }

    /* Check not already closed */
    if (barman_atomic_load(&barman_circular_ram_buffer_configuration.closed)) {
        return BM_NULL;
    }

    /* validate length */
    base_pointer = barman_circular_ram_buffer_configuration.header_data->base_pointer;
    buffer_length = barman_circular_ram_buffer_configuration.header_data->buffer_length;
    if (required_length > buffer_length) {
        return BM_NULL;
    }

    /* get tail offset */
    tail_offset = barman_atomic_load(&barman_circular_ram_buffer_configuration.tail_offset);

    /* check not already got uncommited block */
    if (!barman_atomic_cmp_ex_strong_value(&(barman_circular_ram_buffer_configuration.reserved_tail_offset[core]), BM_NO_RESERVED_TAIL, tail_offset) != 0) {
        return BM_NULL;
    }

    /*
     * To allocate a block of contiguous memory from the buffer as follows:
     *
     *  1  Reserve some limit point in which no thread can free past
     *  2  Ensure that there is enough space for the new block by freeing records up to the lowest reserve point for any core
     *  3  Adjust the tail to the new pointer
     *
     * Uses atomic CAS to move the tail pointer and will retry if another thread moves the tail in the mean time.
     */

    do {
        bm_atomic_uint64 head_offset, new_tail_offset;
        const bm_datastore_header_length real_tail_offset = tail_offset % buffer_length;
        const bm_datastore_header_length remaining_until_wrap = buffer_length - real_tail_offset;
        const bm_datastore_block_length alignment_size = (remaining_until_wrap >= required_length ? 0 : remaining_until_wrap);
        bm_datastore_block_length * length_pointer;

        /* mark the reserved tail */
        barman_atomic_store(&(barman_circular_ram_buffer_configuration.reserved_tail_offset[core]), tail_offset);

        /* if we need to align to the wrap point, then do that first and 'commit' it as if it were a complete block */
        if (alignment_size > 0) {
            /* just align up if necessary so that (head % buffer_length) is not between [real_tail_offset, buffer_length) */
            if (!barman_circular_ram_buffer_free_to_tail(buffer_length, base_pointer, tail_offset - real_tail_offset)) {
                /* reserved limit met; cannot free enough items */
                barman_atomic_store(&(barman_circular_ram_buffer_configuration.reserved_tail_offset[core]), BM_NO_RESERVED_TAIL);
                return BM_NULL;
            }

            /* at this point there should be enough free space, we increment the tail to include the alignment block */
            new_tail_offset = tail_offset + alignment_size;
            if (!barman_atomic_cmp_ex_strong_pointer(&barman_circular_ram_buffer_configuration.tail_offset, &tail_offset, new_tail_offset)) {
                /* failed; try again */
                continue;
            }

            tail_offset = new_tail_offset;

            /* write the padding block if required to align past the end of the buffer */
            if (alignment_size >= sizeof(bm_datastore_block_length)) {
                /* write a padding block */
                length_pointer = BM_ASSUME_ALIGNED_CAST(bm_datastore_block_length, base_pointer + real_tail_offset);
                *length_pointer = BM_DATASTORE_ENCODE_PADDING_BLOCK(alignment_size - sizeof(bm_datastore_block_length), BM_TRUE);
            }
            else {
                /* too small, just fill with zeros */
                bm_uint8 * pointer = (bm_uint8 *) (base_pointer + real_tail_offset);
                barman_memset(pointer, 0, alignment_size);
            }

            /* commit the alignment block */
            barman_circular_ram_buffer_write_commit(core, (bm_uint8 *) (base_pointer + real_tail_offset), alignment_size);
        }
        /* create our data block */
        else {
            /* ensure we have enough free space */
            if (!barman_circular_ram_buffer_ensure_free(buffer_length, base_pointer, &head_offset, tail_offset, required_length)) {
                /* reserved limit met; cannot free enough items */
                barman_atomic_store(&(barman_circular_ram_buffer_configuration.reserved_tail_offset[core]), BM_NO_RESERVED_TAIL);
                return BM_NULL;
            }

            /* at this point there should be enough free space, we increment the tail */
            new_tail_offset = tail_offset + required_length;
            if (!barman_atomic_cmp_ex_strong_pointer(&barman_circular_ram_buffer_configuration.tail_offset, &tail_offset, new_tail_offset)) {
                /* failed; try again */
                continue;
            }

            /* write the data block, mark as padding for now */
            length_pointer = BM_ASSUME_ALIGNED_CAST(bm_datastore_block_length, base_pointer + ((real_tail_offset + alignment_size) % buffer_length));
            *length_pointer = BM_DATASTORE_ENCODE_PADDING_BLOCK(aligned_length, BM_TRUE);
            block_pointer = ((bm_uint8 *) (length_pointer)) + sizeof(bm_datastore_block_length);
        }
    } while (block_pointer == BM_NULL);

    return block_pointer;
}

void barman_circular_ram_buffer_commit_block(bm_uint32 core, bm_uint8 * user_pointer)
{
    /* Check core number */
    if (core >= BM_CONFIG_MAX_CORES) {
        return;
    }

    /* Check not already closed */
    else if (barman_atomic_load(&barman_circular_ram_buffer_configuration.closed)) {
        return;
    }

    /* Pre tests are valid */
    else {
        const bm_datastore_header_length buffer_length = barman_circular_ram_buffer_configuration.header_data->buffer_length;
        bm_uint8 * const base_pointer = barman_circular_ram_buffer_configuration.header_data->base_pointer;
        bm_uint8 * const block_pointer = user_pointer - sizeof(bm_datastore_block_length);
        bm_uint8 * const buffer_end = base_pointer + buffer_length;
        bm_datastore_block_length * length_pointer = BM_ASSUME_ALIGNED_CAST(bm_datastore_block_length, block_pointer);
        const bm_atomic_uint64 reserved_tail = barman_atomic_load(&(barman_circular_ram_buffer_configuration.reserved_tail_offset[core]));
        bm_datastore_block_length user_length;
        bm_datastore_block_length required_length;

        /* Check pointer in bounds */
        if (block_pointer < base_pointer) {
            return;
        }

        user_length = BM_DATASTORE_GET_LENGTH_VALUE(*length_pointer);
        required_length = user_length + sizeof(bm_datastore_block_length);

        if ((user_length == 0) || ((block_pointer + required_length) > buffer_end)) {
            return;
        }

        /* Check has something to commit */
        if (reserved_tail == BM_NO_RESERVED_TAIL) {
            return;
        }

        /* Mark the length as valid */
        *length_pointer = BM_DATASTORE_ENCODE_PADDING_BLOCK(user_length, BM_FALSE);

        /* commit the data block */
        barman_circular_ram_buffer_write_commit(core, block_pointer, required_length);
    }
}

void barman_circular_ram_buffer_close(void)
{
    barman_atomic_store(&barman_circular_ram_buffer_configuration.closed, BM_TRUE);
}
