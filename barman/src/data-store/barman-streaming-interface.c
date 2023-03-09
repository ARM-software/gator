/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#include "data-store/barman-streaming-interface.h"

#include "barman-atomics.h"
#include "barman-cache.h"
#include "barman-config.h"
#include "barman-intrinsics.h"
#include "barman-log.h"
#include "data-store/barman-ext-streaming-backend.h"
#include "data-store/barman-itm.h"
#include "data-store/barman-stm.h"
#include "multicore/barman-multicore.h"

#if BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_ITM
#define BM_NUMBER_OF_STREAMING_CHANNELS                                    BM_CONFIG_ITM_NUMBER_OF_PORTS
#define BM_NUMBER_OF_STREAMING_BANKS                                       1
#define barman_streaming_backend_get_bank()                                0
#define barman_streaming_backend_init(config)                              barman_itm_init(config)
#define barman_streaming_backend_write_frame(data, length, channel, flush) barman_itm_write_frame(data, length, (channel) + BM_CONFIG_ITM_MIN_PORT_NUMBER, flush)
#define barman_streaming_backend_close()

#elif BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_STM
#define BM_NUMBER_OF_STREAMING_CHANNELS                                    BM_CONFIG_STM_NUMBER_OF_CHANNELS
/* STM is banked by master ID which could be per core or cluster but lets assume not banked to cover all cases */
#define BM_NUMBER_OF_STREAMING_BANKS                                       1
#define barman_streaming_backend_get_bank()                                0
#define barman_streaming_backend_init(config)                              barman_stm_init(config)
#define barman_streaming_backend_write_frame(data, length, channel, flush) barman_stm_write_frame(data, length, (channel) + BM_CONFIG_STM_MIN_CHANNEL_NUMBER, flush)
#define barman_streaming_backend_close()

#elif BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_STREAMING_USER_SUPPLIED
#ifndef BM_CONFIG_STREAMING_DATASTORE_USER_SUPPLIED_NUMBER_OF_CHANNELS
#error "BM_CONFIG_STREAMING_DATASTORE_USER_SUPPLIED_NUMBER_OF_CHANNELS must be defined"
#endif
#define BM_NUMBER_OF_STREAMING_CHANNELS                                    BM_CONFIG_STREAMING_DATASTORE_USER_SUPPLIED_NUMBER_OF_CHANNELS
#ifndef BM_CONFIG_STREAMING_DATASTORE_USER_SUPPLIED_NUMBER_OF_BANKS
#error "BM_CONFIG_STREAMING_DATASTORE_USER_SUPPLIED_NUMBER_OF_BANKS must be defined"
#endif
#define BM_NUMBER_OF_STREAMING_BANKS                                       BM_CONFIG_STREAMING_DATASTORE_USER_SUPPLIED_NUMBER_OF_BANKS
#define barman_streaming_backend_get_bank()                                barman_ext_streaming_backend_get_bank()
#define barman_streaming_backend_init(config)                              barman_ext_streaming_backend_init(config)
#define barman_streaming_backend_write_frame(data, length, channel, flush) barman_ext_streaming_backend_write_frame(data, length, channel, flush)
#define barman_streaming_backend_close()                                   barman_ext_streaming_backend_close()

#else /* Define some dummy values so it always compiles without warnings */
#define BM_NUMBER_OF_STREAMING_CHANNELS                                    1
#define BM_NUMBER_OF_STREAMING_BANKS                                       1
#define barman_streaming_backend_get_bank()                                0
#define barman_streaming_backend_init(config)                              (*((bm_uint8 *) &config) != 0)
#define barman_streaming_backend_write_frame(data, length, channel, flush) do { (void)(data); (void)(length); (void)(channel); (void)(flush);} while (0)
#define barman_streaming_backend_close()

#endif

#define BM_STREAMING_BUFFER_SIZE 0x100

#define BM_NUMBER_OF_STREAMING_BUFFERS    (BM_NUMBER_OF_STREAMING_BANKS * BM_NUMBER_OF_STREAMING_CHANNELS)

/* *********************************** */

/**
 * @brief   Defines the data
 * @ingroup bm_data_store_streaming_interface
 */
struct barman_streaming_interface_configuration
{
    /** status flag */
    bm_atomic_uintptr status;
    /** A flag for each buffer to indicate it's busy */
    bm_atomic_bool buffer_is_busy[BM_NUMBER_OF_STREAMING_BUFFERS];
};

#define BM_STATUS_CLOSED    0
#define BM_STATUS_OPEN      1
#define BM_STATUS_CHANGING  2

/** The configuration settings */
static struct barman_streaming_interface_configuration barman_streaming_interface_configuration = { BM_STATUS_CLOSED, { BM_FALSE } };

/** The temporary buffer to store data that is to be written out via STREAMING */
static bm_uint8 buffers[BM_STREAMING_BUFFER_SIZE * BM_NUMBER_OF_STREAMING_BUFFERS] BM_ALIGN(16);

/**
 * Gets an available buffer
 *
 * @return A free buffer (0 based) or -1 if they are all busy
 */
static bm_int32 get_a_buffer()
{
    bm_uint16 buffer_no;
    const bm_uint16 bank_start = barman_streaming_backend_get_bank() * BM_NUMBER_OF_STREAMING_CHANNELS;

    if (bank_start >= BM_NUMBER_OF_STREAMING_BUFFERS)
    {
        BM_ERROR("barman_streaming_backend_get_bank() returned value >= BM_NUMBER_OF_STREAMING_BUFFERS");
        return -1;
    }

    /* Find a buffer_no not already in use */
    for (buffer_no = bank_start; buffer_no < bank_start + BM_NUMBER_OF_STREAMING_CHANNELS; ++buffer_no)
    {
        if (barman_atomic_cmp_ex_strong_value(&(barman_streaming_interface_configuration.buffer_is_busy[buffer_no]), BM_FALSE, BM_TRUE)) {
            break;
        }
    }

    if (buffer_no == bank_start + BM_NUMBER_OF_STREAMING_CHANNELS)
    {
        /* Couldn't find a spare buffer */
        return -1;
    }

    return buffer_no;
}

/**
 * Returns a buffer allowing it to be reused
 *
 * @param buffer_no    The buffer to return (0 based)
 */
static void return_buffer(bm_uint16 buffer_no)
{
    /* Clear buffer busy flag */
    barman_atomic_store(&(barman_streaming_interface_configuration.buffer_is_busy[buffer_no]), BM_FALSE);
}

/* *********************************** */

bm_bool barman_streaming_interface_initialize(bm_datastore_config datastore_config)
{
    /* Can only change the settings if closed */
    if (!barman_atomic_cmp_ex_strong_value(&barman_streaming_interface_configuration.status, BM_STATUS_CLOSED, BM_STATUS_CHANGING)) {
        return BM_FALSE;
    }

    if (barman_streaming_backend_init(datastore_config))
    {
        barman_atomic_store(&(barman_streaming_interface_configuration.status), BM_STATUS_OPEN);
        return BM_TRUE;
    }

    /* Failed to initialize, so close again */
    barman_atomic_store(&(barman_streaming_interface_configuration.status), BM_STATUS_CLOSED);
    return BM_FALSE;
}

bm_uint8 * barman_streaming_interface_get_block(bm_datastore_block_length user_length)
{
    const bm_datastore_block_length required_length = user_length + sizeof(bm_datastore_block_length);
    bm_uint8 * block_pointer;
    bm_int32 buffer_no;

    /* Check length */
    if (user_length == 0 || required_length > BM_STREAMING_BUFFER_SIZE) {
        return BM_NULL;
    }

    /* Check not already closed */
    if (barman_atomic_load(&barman_streaming_interface_configuration.status) != BM_STATUS_OPEN) {
        return BM_NULL;
    }

    buffer_no = get_a_buffer();

    if (buffer_no == -1)
    {
        return BM_NULL;
    }

    /* Configure the result */
    block_pointer = buffers + BM_STREAMING_BUFFER_SIZE * buffer_no;

    /* Write the length value */
    *BM_ASSUME_ALIGNED_CAST(bm_datastore_block_length, block_pointer) = user_length;

    return block_pointer + sizeof(bm_datastore_block_length);
}

void barman_streaming_interface_commit_block(bm_uint8 * user_pointer)
{
    bm_uint8 * const block_pointer = user_pointer - sizeof(bm_datastore_block_length);
    bm_uintptr const byte_offset = block_pointer - buffers;
    const bm_uint16 buffer_no = byte_offset / BM_STREAMING_BUFFER_SIZE;
    const bm_uint16 bank = buffer_no / BM_NUMBER_OF_STREAMING_CHANNELS;
    const bm_uint16 channel = buffer_no % BM_NUMBER_OF_STREAMING_CHANNELS;

    /* Check pointer and buffer is valid */
    if ((byte_offset % BM_STREAMING_BUFFER_SIZE) != 0 ||
        buffer_no >= BM_NUMBER_OF_STREAMING_BUFFERS ||
        bank != barman_streaming_backend_get_bank() ||
        !barman_atomic_load(&barman_streaming_interface_configuration.buffer_is_busy[buffer_no])) {
        return;
    }

    /* buffer is now thought to be valid */

    /* Check not already closed */
    if (barman_atomic_load(&barman_streaming_interface_configuration.status) == BM_STATUS_OPEN) {
        /* Pre tests are valid */
        bm_datastore_block_length * length_pointer = BM_ASSUME_ALIGNED_CAST(bm_datastore_block_length, block_pointer);
        bm_datastore_block_length user_length;
        bm_datastore_block_length required_length;

        user_length = BM_DATASTORE_GET_LENGTH_VALUE(*length_pointer);
        required_length = user_length + sizeof(bm_datastore_block_length);

        if ((user_length != 0) && (required_length <= BM_STREAMING_BUFFER_SIZE - sizeof(bm_datastore_block_length))) {
            barman_streaming_backend_write_frame(user_pointer, user_length, channel, BM_FALSE);
        }

    }

    return_buffer(buffer_no);
}

void barman_streaming_interface_close(void)
{
    barman_atomic_store(&barman_streaming_interface_configuration.status, BM_STATUS_CHANGING);
    barman_streaming_backend_close();
    barman_atomic_store(&barman_streaming_interface_configuration.status, BM_STATUS_CLOSED);
}

BM_NONNULL((2))
void barman_streaming_interface_notify_header_updated(bm_uint64 timestamp, const void * header, bm_uintptr length)
{
    /* We don't actually need the buffer but we need to reserve the associated channel */
    const bm_int32 buffer_no = get_a_buffer();
    if (buffer_no != -1)
    {
        const bm_uint16 channel = buffer_no % BM_NUMBER_OF_STREAMING_CHANNELS;
        barman_streaming_backend_write_frame((const bm_uint8 *)header, length, channel, BM_TRUE);
        return_buffer(buffer_no);
    }
}
