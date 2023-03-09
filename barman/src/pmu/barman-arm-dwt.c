/* Copyright (C) 2017-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "pmu/barman-arm-dwt.h"
#include "barman-types.h"
#include "m-profile/barman-arch-constants.h"
#include "m-profile/barman-debug-control.h"

/**
 * @defgroup    bm_dwt     Macros for using DWT
 * @{ */

#define BM_READ_DWT_CTRL(x)    x = BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(dwt_base))

#define BM_WRITE_DWT_CTRL(x)   BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(dwt_base)) = x
#define BM_WRITE_DWT_LAR(x)    BM_MEMORY_MAPPED_REGISTER_32((bm_uintptr)(dwt_base) + 0xFB0) = x

/* NUMCOMP, bits[31:28] */
#define BM_DWT_CTRL_NOTRCPKT_BIT      (BM_UINT32(1) << 27)
#define BM_DWT_CTRL_NOEXTTRIG_BIT     (BM_UINT32(1) << 26)
#define BM_DWT_CTRL_NOCYCCNT_BIT      (BM_UINT32(1) << 25)
#define BM_DWT_CTRL_NOPRFCNT_BIT      (BM_UINT32(1) << 24)
#define BM_DWT_CTRL_CYCEVTENA_BIT     (BM_UINT32(1) << 22)
#define BM_DWT_CTRL_FOLDEVTENA_BIT    (BM_UINT32(1) << 21)
#define BM_DWT_CTRL_LSUEVTENA_BIT     (BM_UINT32(1) << 20)
#define BM_DWT_CTRL_SLEEPEVTENA_BIT   (BM_UINT32(1) << 19)
#define BM_DWT_CTRL_EXCEVTENA_BIT     (BM_UINT32(1) << 18)
#define BM_DWT_CTRL_CPIEVTENA_BIT     (BM_UINT32(1) << 17)
#define BM_DWT_CTRL_EXCTRCENA_BIT     (BM_UINT32(1) << 16)
#define BM_DWT_CTRL_PCSAMPLENA_BIT    (BM_UINT32(1) << 12)
#define BM_DWT_CTRL_SYNCTAP_MASK      BM_UINT32(0xc00)
#define BM_DWT_CTRL_SYNCTAP_SHIFT     10
#define BM_DWT_CTRL_CYCTAP_BIT        (BM_UINT32(1) << 9)
#define BM_DWT_CTRL_POSTINIT_MASK     BM_UINT32(0x1e0)
#define BM_DWT_CTRL_POSTPRESET_MASK   BM_UINT32(0x1e)
#define BM_DWT_CTRL_POSTPRESET_SHIFT  1
#define BM_DWT_CTRL_CYCCNTENA_BIT     (BM_UINT32(1) << 0)

#define BM_DWT_CTRL_ENABLE_MASK       (BM_DWT_CTRL_CYCEVTENA_BIT \
                                      | BM_DWT_CTRL_FOLDEVTENA_BIT \
                                      | BM_DWT_CTRL_LSUEVTENA_BIT \
                                      | BM_DWT_CTRL_SLEEPEVTENA_BIT \
                                      | BM_DWT_CTRL_EXCEVTENA_BIT \
                                      | BM_DWT_CTRL_CPIEVTENA_BIT \
                                      | BM_DWT_CTRL_EXCTRCENA_BIT \
                                      | BM_DWT_CTRL_PCSAMPLENA_BIT)

#define BM_DWT_CTRL_BASE_EVTENA_BIT   BM_DWT_CTRL_CPIEVTENA_BIT

/** @} */

/** Address of the DWT registers */
static void * const dwt_base = BM_DWT_BASE_ADDRESS;

static bm_uint32 ctrl_enable_bits;

bm_int32 barman_arm_dwt_init(bm_bool enable_pc_sampling,
                             bm_bool enable_exception_tracing,
                             bm_uint32 n_event_types, const bm_uint32 * event_types,
                             bm_uint32 cycle_counter_overflow)
{
    bm_uint32 i, ctrl, postcnt_reload;

#if BM_ARM_ARCH_PROFILE == 'M'
    /* Enable trace */
    bm_uint32 demcr;
    BM_READ_DEMCR(demcr);
    demcr |= BM_DEMCR_TRCENA_BIT;
    BM_WRITE_DEMCR(demcr);
#endif

    /* Unlock the configuration registers */
    BM_WRITE_DWT_LAR(0xC5ACCE55);

    BM_READ_DWT_CTRL(ctrl);

    /* Make sure the cycle counter and performance counters are supported */
    if (ctrl & (BM_DWT_CTRL_NOCYCCNT_BIT | BM_DWT_CTRL_NOPRFCNT_BIT))
        return -1;

    /* Clear the enable bits */
    ctrl_enable_bits = 0;

    /* Set the overflow event bits */
    for (i = 0; i < n_event_types; ++i)
    {
        const bm_uint32 event_type = event_types[i];

        /*
         * If PC sampling is enabled, setting CYCEVTENA (event_type 5)
         * is deprecated as it generates just a PC sample packet anyway.
         */
        if (event_type < 5 || (event_type == 5 && !enable_pc_sampling))
        {
            ctrl_enable_bits |= BM_DWT_CTRL_BASE_EVTENA_BIT << event_type;
        }
    }

    if (enable_exception_tracing)
    {
        ctrl_enable_bits |= BM_DWT_CTRL_EXCTRCENA_BIT;
    }

    if (enable_pc_sampling)
    {
        ctrl_enable_bits |= BM_DWT_CTRL_PCSAMPLENA_BIT;
    }

    /* Set the current postcnt to zero */
    ctrl &= ~BM_DWT_CTRL_POSTINIT_MASK;

    /* Set the postcnt reload value */
    if (cycle_counter_overflow > (BM_UINT32(1) << 10))
    {
        ctrl |= BM_DWT_CTRL_CYCTAP_BIT;

        if (cycle_counter_overflow > (BM_UINT32(16) << 10))
        {
            postcnt_reload = BM_UINT32(15);
        }
        else
        {
            postcnt_reload = (cycle_counter_overflow >> 10) - 1;
        }
    }
    else
    {
        ctrl &= ~BM_DWT_CTRL_CYCTAP_BIT;

        if (cycle_counter_overflow < (BM_UINT32(1) << 6))
        {
            postcnt_reload = BM_UINT32(0);
        }
        else
        {
            postcnt_reload = (cycle_counter_overflow >> 6) - 1;
        }
    }
    ctrl &= ~BM_DWT_CTRL_POSTPRESET_MASK;
    ctrl |= (postcnt_reload << BM_DWT_CTRL_POSTPRESET_SHIFT) & BM_DWT_CTRL_POSTPRESET_MASK;

    /* Set synchronization packets every 16M cycles */
    ctrl &= ~BM_DWT_CTRL_SYNCTAP_MASK;
    ctrl |= BM_UINT32(1) << BM_DWT_CTRL_SYNCTAP_SHIFT;

    /* Enable the cycle counter */
    ctrl |= BM_DWT_CTRL_CYCCNTENA_BIT;

    /* Write the configuration to the control register */
    BM_WRITE_DWT_CTRL(ctrl);

    return 0;
}

void barman_arm_dwt_start(void)
{
    bm_uint32 ctrl;
    BM_READ_DWT_CTRL(ctrl);
    ctrl &= ~BM_DWT_CTRL_ENABLE_MASK;
    ctrl |= ctrl_enable_bits;
    BM_WRITE_DWT_CTRL(ctrl);
}

void barman_arm_dwt_stop(void)
{
    bm_uint32 ctrl;
    BM_READ_DWT_CTRL(ctrl);
    ctrl &= ~BM_DWT_CTRL_ENABLE_MASK;
    BM_WRITE_DWT_CTRL(ctrl);
}
