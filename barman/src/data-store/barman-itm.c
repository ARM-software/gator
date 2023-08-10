/* Copyright (C) 2017-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#include "data-store/barman-itm.h"
#include "barman-atomics.h"
#include "m-profile/barman-debug-control.h"

/**
 * @defgroup    bm_itm     Macros for using ITM
 * @{ */

#define BM_READ_ITM_TER(port_block, x)    x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(itm_base) + 0xE00 + (port_block) * 0x04)
#define BM_READ_ITM_TPR(x)                x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(itm_base) + 0xE40)
#define BM_READ_ITM_TCR(x)                x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(itm_base) + 0xE80)
#define BM_READ_ITM_LSR(x)                x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(itm_base) + 0xFB4)

/* Note we don't use BM_MEMORY_MAPPED_REGISTER here because that is volatile and our atomic
 * functions don't have volatile parameters. However, we make the assumption they handle the
 * address in a volatile way, i.e., they only make exactly one write to the address per call
 * (if the CAS is successful, zero writes otherwise)
 */
#define BM_WRITE_ITM_STIM_WHEN_READY(port, value, bits)                                                                \
    do {                                                                                                               \
        volatile bm_uint##bits * pointer = (volatile bm_uint##bits *) ((bm_uintptr) itm_base + (port) *0x04);          \
        while (BM_ITM_STIM_FIFOREADY_BIT != *pointer) {                                                                \
        }                                                                                                              \
        *pointer = value;                                                                                              \
    } while (0)

#define BM_WRITE_ITM_STIM_8(port, value)  BM_WRITE_ITM_STIM_WHEN_READY(port, value, 8)
#define BM_WRITE_ITM_STIM_16(port, value) BM_WRITE_ITM_STIM_WHEN_READY(port, value, 16)
#define BM_WRITE_ITM_STIM_32(port, value) BM_WRITE_ITM_STIM_WHEN_READY(port, value, 32)

#define BM_WRITE_ITM_TER(port_block, x)   BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(itm_base) + 0xE00 + (port_block) * 0x04) = x
#define BM_WRITE_ITM_TPR(x)               BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(itm_base) + 0xE40) = x
#define BM_WRITE_ITM_TCR(x)               BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(itm_base) + 0xE80) = x
#define BM_WRITE_ITM_LAR(x)               BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(itm_base) + 0xFB0) = x

/** @} */

#define BM_ITM_STIM_FIFOREADY_BIT    (BM_UINT32(1) << 0)

#define BM_ITM_TCR_BUSY_BIT          (BM_UINT32(1) << 23)
#define BM_ITM_TCR_TRACEID_MASK      BM_UINT32(0x007f0000)
#define BM_ITM_TCR_TRACEID_SHIFT     16
#define BM_ITM_TCR_GTSFREQ_MASK      BM_UINT32(0xc00)
#define BM_ITM_TCR_GTSFREQ_SHIFT     10
#define BM_ITM_TCR_TSPrescale_MASK   BM_UINT32(0x300)
#define BM_ITM_TCR_TSPrescale_SHIFT  8
#define BM_ITM_TCR_SWOENA_BIT        (BM_UINT32(1) << 4)
#define BM_ITM_TCR_TXENA_BIT         (BM_UINT32(1) << 3)
#define BM_ITM_TCR_SYNCENA_BIT       (BM_UINT32(1) << 2)
#define BM_ITM_TCR_TSENA_BIT         (BM_UINT32(1) << 1)
#define BM_ITM_TCR_ITMENA_BIT        (BM_UINT32(1) << 0)


#ifndef BM_ITM_TRACE_ID
#define BM_ITM_TRACE_ID -1
#endif

/** Address of the ITM registers */
static void * itm_base = BM_NULL;

static void enable_ports(void)
{
    bm_uint32 ter;

    const bm_uint8 max_port_block = (BM_CONFIG_ITM_NUMBER_OF_PORTS + BM_CONFIG_ITM_MIN_PORT_NUMBER) / 32;
    const bm_uint8 min_port_block = BM_CONFIG_ITM_MIN_PORT_NUMBER / 32;
    const bm_uint8 max_port_bit = (BM_CONFIG_ITM_NUMBER_OF_PORTS + BM_CONFIG_ITM_MIN_PORT_NUMBER) % 32;
    const bm_uint8 min_port_bit = BM_CONFIG_ITM_MIN_PORT_NUMBER % 32;

    if (min_port_block == max_port_block)
    {
        BM_READ_ITM_TER(min_port_block, ter);
        ter |= ~(~BM_UINT32(0) << (max_port_bit - min_port_bit)) << min_port_bit;
        BM_WRITE_ITM_TER(min_port_block, ter);
    }
    else
    {
        int i;

        BM_READ_ITM_TER(min_port_block, ter);
        ter |= ~BM_UINT32(0) << min_port_bit;
        BM_WRITE_ITM_TER(min_port_block, ter);

        for (i = min_port_block + 1; i < max_port_block; ++i)
        {
            BM_WRITE_ITM_TER(i, ~BM_UINT32(0));
        }

        if (max_port_bit > 0)
        {
            BM_READ_ITM_TER(max_port_block, ter);
            ter |= BM_UINT32(1) << (max_port_bit - 1);
            BM_WRITE_ITM_TER(max_port_block, ter);
        }
    }
}


static void enable_unprivileged_access(void)
{
    bm_uint32 tpr;

    const bm_uint8 num_priv_bits = (BM_CONFIG_ITM_NUMBER_OF_PORTS + 7) / 8;
    const bm_uint8 min_priv_bit = BM_CONFIG_ITM_MIN_PORT_NUMBER / 8;

    BM_READ_ITM_TPR(tpr);
    tpr &= ~(~(~BM_UINT32(0) << num_priv_bits) << min_priv_bit);
    BM_WRITE_ITM_TPR(tpr);
}

bm_bool barman_itm_init(struct bm_datastore_config_itm config)
{
    bm_uint32 tcr;

    itm_base = config.registers;

    if (itm_base == BM_NULL)
    {
        return BM_FALSE;
    }

#if BM_ARM_ARCH_PROFILE == 'M'
    {
        /* Enable trace */
        bm_uint32 demcr;
        BM_READ_DEMCR(demcr);
        demcr |= BM_DEMCR_TRCENA_BIT;
        BM_WRITE_DEMCR(demcr);
    }
#endif

    /* Unlock the configuration registers */
    BM_WRITE_ITM_LAR(0xC5ACCE55);

    /* disable */
    BM_READ_ITM_TCR(tcr);
    BM_WRITE_ITM_TCR(tcr & ~BM_ITM_TCR_ITMENA_BIT);

    /* and wait till not busy */
    do BM_READ_ITM_TCR(tcr);
    while (tcr & BM_ITM_TCR_BUSY_BIT);

    enable_ports();
    enable_unprivileged_access();

    /* Set the trace ID if needed */
#if BM_ITM_TRACE_ID >= 0
    tcr &= ~BM_ITM_TCR_TRACEID_MASK;
    tcr |= (BM_ITM_TRACE_ID << BM_ITM_TCR_TRACEID_SHIFT) & BM_ITM_TCR_TRACEID_MASK;
#endif

    /* Disable global timestamps */
    tcr &= ~BM_ITM_TCR_GTSFREQ_MASK;

    /* Don't prescale local timestamps */
    tcr &= ~BM_ITM_TCR_TSPrescale_MASK;

    /* Use processor clock for local timestamps */
    tcr &= ~BM_ITM_TCR_SWOENA_BIT;

    /* Enable the DWT packets */
    tcr |= BM_ITM_TCR_TXENA_BIT;

    /* Enable the synchronization packets */
    tcr |= BM_ITM_TCR_SYNCENA_BIT;

    /* Enable local timestamps */
    tcr |= BM_ITM_TCR_TSENA_BIT;

    /* Enable the ITM */
    tcr |= BM_ITM_TCR_ITMENA_BIT;

    BM_WRITE_ITM_TCR(tcr);

    return BM_TRUE;
}

void barman_itm_write_frame(const bm_uint8 * data, bm_uintptr length, bm_uint16 port, bm_bool flush)
{
    const bm_uintptr word_start_address = (((bm_uintptr)data + 3) & ~0x3ul);
    const bm_uintptr word_end_address = ((bm_uintptr)data + length) & ~0x3ul;

    const bm_uint8 * start_bytes = data;
    const bm_uintptr num_start_bytes = word_start_address - (bm_uintptr)data;

    const bm_uint32 * aligned_words = (const bm_uint32 *) word_start_address;
    const bm_uintptr num_aligned_words = (word_end_address - word_start_address) / 4;

    const bm_uint8 * end_bytes = (const bm_uint8 *) word_end_address;
    const bm_uintptr num_end_bytes = length - 4 * num_aligned_words - num_start_bytes;

    bm_uintptr i;

    /* Write the data out to ITM using a 16 bit packet to mark the start of a frame */
    BM_WRITE_ITM_STIM_16(port, 0xffffu);

    /* Write out the rest */

    for (i = 0; i < num_start_bytes; ++i)
    {
        BM_WRITE_ITM_STIM_8(port, start_bytes[i]);
    }

    for (i = 0; i < num_aligned_words; ++i)
    {
        bm_uint32 word = aligned_words[i];
#if BM_BIG_ENDIAN
        /* Our decoder assumes the packets are little endian */
        word = BM_SWAP_ENDIANESS_32(word);
#endif

        BM_WRITE_ITM_STIM_32(port, word);
    }

    for (i = 0; i < num_end_bytes; ++i)
    {
        BM_WRITE_ITM_STIM_8(port, end_bytes[i]);
    }

    if (flush)
    {
        /*
         * Writing a 16 bit packet ends the frame without having to wait
         * for the start of the next frame.
         */
        BM_WRITE_ITM_STIM_16(port, 0xffffu);
    }
}
