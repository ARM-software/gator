/* Copyright (C) 2017-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#include "data-store/barman-stm.h"

/**
 * @defgroup    bm_stm     Macros for using STM
 * @{ */

#define BM_WRITE_STM_EXTENDED_STIMULUS(base_address, channel, trace_packet_type, value, bits) \
    BM_MEMORY_MAPPED_REGISTER_ ## bits ((bm_uintptr)(base_address) + (channel) * 0x100 + trace_packet_type) = value

#define BM_WRITE_STM_EXTENDED_STIMULUS_8(base_address, channel, trace_packet_type, value) \
    BM_WRITE_STM_EXTENDED_STIMULUS(base_address, channel, trace_packet_type, value, 8)

#define BM_WRITE_STM_EXTENDED_STIMULUS_16(base_address, channel, trace_packet_type, value) \
    BM_WRITE_STM_EXTENDED_STIMULUS(base_address, channel, trace_packet_type, value, 16)

#define BM_WRITE_STM_EXTENDED_STIMULUS_32(base_address, channel, trace_packet_type, value) \
    BM_WRITE_STM_EXTENDED_STIMULUS(base_address, channel, trace_packet_type, value, 32)

#define BM_WRITE_STM_EXTENDED_STIMULUS_64(base_address, channel, trace_packet_type, value) \
    BM_WRITE_STM_EXTENDED_STIMULUS(base_address, channel, trace_packet_type, value, 64)

/* Guaranteed data accesses */
#define BM_STM_G_DMTS   0x00 /* Data, marked with timestamp, guaranteed */
#define BM_STM_G_DM     0x08 /* Data, marked, guaranteed */
#define BM_STM_G_DTS    0x10 /* Data, with timestamp, guaranteed */
#define BM_STM_G_D      0x18 /* Data, guaranteed */
/* Guaranteed non-data accesses */
#define BM_STM_G_FLAGTS 0x60 /* Flag with timestamp, guaranteed */
#define BM_STM_G_FLAG   0x68 /* Flag, guaranteed */
#define BM_STM_G_TRIGTS 0x70 /* Trigger with timestamp, guaranteed */
#define BM_STM_G_TRIG   0x78 /* Trigger, guaranteed */
/* Invariant Timing data accesses */
#define BM_STM_I_DMTS   0x80 /* Data, marked with timestamp, invariant timing */
#define BM_STM_I_DM     0x88 /* Data, marked, invariant timing */
#define BM_STM_I_DTS    0x90 /* Data, with timestamp, invariant timing */
#define BM_STM_I_D      0x98 /* Data, invariant timing */
/* Invariant Timing non-data accesses */
#define BM_STM_I_FLAGTS 0xE0 /* Flag with timestamp, invariant timing */
#define BM_STM_I_FLAG   0xE8 /* Flag, invariant timing */
#define BM_STM_I_TRIGTS 0xF0 /* Trigger with timestamp, invariant timing */
#define BM_STM_I_TRIG   0xF8 /* Trigger, invariant timing */

/* Stimulus Port Control Registers */
#define BM_READ_STMSPER(control_block, x)         x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE00)
#define BM_READ_STMSPTER(control_block, x)        x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE20)
#define BM_READ_STMPRIVMASKR(control_block, x)    x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE40)
#define BM_READ_STMSPSCR(control_block, x)        x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE60)
#define BM_READ_STMSPMSCR(control_block, x)       x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE64)
#define BM_READ_STMSPOVERIDER(control_block, x)   x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE68)
#define BM_READ_STMSPMOVERIDER(control_block, x)  x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE6C)
#define BM_READ_STMSPTRIGCSR(control_block, x)    x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE70)

#define BM_WRITE_STMSPER(control_block, x)        BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE00) = x
#define BM_WRITE_STMSPTER(control_block, x)       BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE20) = x
#define BM_WRITE_STMPRIVMASKR(control_block, x)   BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE40) = x
#define BM_WRITE_STMSPSCR(control_block, x)       BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE60) = x
#define BM_WRITE_STMSPMSCR(control_block, x)      BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE64) = x
#define BM_WRITE_STMSPOVERIDER(control_block, x)  BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE68) = x
#define BM_WRITE_STMSPMOVERIDER(control_block, x) BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE6C) = x
#define BM_WRITE_STMSPTRIGCSR(control_block, x)   BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE70) = x

/* Primary Control and Status Registers */
#define BM_READ_STMTCSR(control_block, x)         x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE80)
  /* write-only STMTSSTIMR */
#define BM_READ_STMTSFREQR(control_block, x)      x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE8C)
#define BM_READ_STMSYNCR(control_block, x)        x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE90)
#define BM_READ_STMAUXCR(control_block, x)        x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE94)

#define BM_WRITE_STMTCSR(control_block, x)        BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE80) = x
#define BM_WRITE_STMTSSTIMR(control_block, x)     BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE84) = x
#define BM_WRITE_STMTSFREQR(control_block, x)     BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE8C) = x
#define BM_WRITE_STMSYNCR(control_block, x)       BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE90) = x
#define BM_WRITE_STMAUXCR(control_block, x)       BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xE94) = x

/* Identification Registers */
#define BM_READ_STMFEAT1R(control_block, x)       x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xEA0)
#define BM_READ_STMFEAT2R(control_block, x)       x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xEA4)
#define BM_READ_STMFEAT3R(control_block, x)       x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xEA8)

/* CoreSight Management Registers */
#define BM_READ_STMITCTRL(control_block, x)       x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xF00)
#define BM_READ_STMCLAIMSET(control_block, x)     x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xFA0)
#define BM_READ_STMCLAIMCLR(control_block, x)     x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xFA4)
  /* write-only STMLAR */
#define BM_READ_STMLSR(control_block, x)          x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xFB4)
#define BM_READ_STMAUTHSTATUS(control_block, x)   x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xFB8)
#define BM_READ_STMDEVARCH(control_block, x)      x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xFBC)
#define BM_READ_STMDEVID(control_block, x)        x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xFC8)
#define BM_READ_STMDEVTYPE(control_block, x)      x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xFCC)

#define BM_WRITE_STMITCTRL(control_block, x)      BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xF00) = x
#define BM_WRITE_STMCLAIMSET(control_block, x)    BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xFA0) = x
#define BM_WRITE_STMCLAIMCLR(control_block, x)    BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xFA4) = x
#define BM_WRITE_STMLAR(control_block, x)         BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(control_block) + 0xFB0) = x

/** @} */

#define BM_STMTCSR_TRACEID_SHIFT (16)
#define BM_STMTCSR_TRACEID_MASK  (0x7f0000)
#define BM_STMTCSR_EN_BIT        (0x1)

#ifndef BM_STM_TRACE_ID
#define BM_STM_TRACE_ID -1
#endif

/** Address of the STM extended stimulus ports */
static void * extended_stimulus_ports = BM_NULL;

bm_bool barman_stm_init(struct bm_datastore_config_stm config)
{
    bm_uint32 tcsr;

    extended_stimulus_ports = config.extended_stimulus_ports;

    if (extended_stimulus_ports == BM_NULL)
    {
        return BM_FALSE;
    }

    if (config.configuration_registers != BM_NULL)
    {
        /* Unlock the configuration registers */
        BM_WRITE_STMLAR(config.configuration_registers, 0xC5ACCE55);

        /* Enable all ports; we assume the user would've passed NULL
         * and configured it them selves if they didn't want that */
        BM_WRITE_STMSPER(config.configuration_registers, 0xFFFFFFFF);

        BM_READ_STMTCSR(config.configuration_registers, tcsr);

        /* Set the trace ID if needed */
#if BM_STM_TRACE_ID >= 0
        tcsr &= ~BM_STMTCSR_TRACEID_MASK;
        tcsr |= (BM_STM_TRACE_ID << BM_STMTCSR_TRACEID_SHIFT) & BM_STMTCSR_TRACEID_MASK;
#endif

        /* Enable STM */
        tcsr |= BM_STMTCSR_EN_BIT;

        BM_WRITE_STMTCSR(config.configuration_registers, tcsr);
    }

    return BM_TRUE;
}

void barman_stm_write_frame(const bm_uint8 * data, bm_uintptr length, bm_uint16 channel, bm_bool flush)
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

    /* Write the data out to STM using a flag to mark the start of a frame */
    BM_WRITE_STM_EXTENDED_STIMULUS_8(extended_stimulus_ports, channel, BM_STM_G_FLAG, 0);

    /* Write out the rest without markers */

    for (i = 0; i < num_start_bytes; ++i)
    {
        BM_WRITE_STM_EXTENDED_STIMULUS_8(extended_stimulus_ports, channel, BM_STM_G_D, start_bytes[i]);
    }

    for (i = 0; i < num_aligned_words; ++i)
    {
        bm_uint32 word = aligned_words[i];
#if BM_BIG_ENDIAN
        /* Our decoder assumes the packets are little endian */
        word = BM_SWAP_ENDIANESS_32(word);
#endif

        BM_WRITE_STM_EXTENDED_STIMULUS_32(extended_stimulus_ports, channel, BM_STM_G_D, word);
    }

    for (i = 0; i < num_end_bytes; ++i)
    {
        BM_WRITE_STM_EXTENDED_STIMULUS_8(extended_stimulus_ports, channel, BM_STM_G_D, end_bytes[i]);
    }

    if (flush)
    {
        /* Writing a flag ends the frame without having to wait for the start of the next frame */
        BM_WRITE_STM_EXTENDED_STIMULUS_8(extended_stimulus_ports, channel, BM_STM_G_FLAG, 0);
    }
}

