/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#include "barman-types.h"

#if BM_COMPILER_IS_ARMCC

/* Don't warn that these hide builtin functions because the C library is missing them */
#pragma push
#pragma diag_suppress 2687

__asm bm_uint64 __sync_fetch_and_add_8(bm_uint64* ptr, bm_uint64 val)
{
    push   {r4, r5, r6, r7}
    dmb    ish
1
    ldrexd r4, r5, [r0]
    adds   r6, r4, r2
    adc    r7, r5, r3
    strexd r1, r6, r7, [r0]
    cmp    r1, #0
    bne    %b1
    mov    r0, r4
    mov    r1, r5
    dmb    ish
    pop    {r4, r5, r6, r7}
    bx     lr
}

__asm bm_uint64 __sync_sub_and_fetch_8(bm_uint64* ptr, bm_uint64 val)
{
    push   {r4, r5}
    dmb    ish
1
    ldrexd r4, r5, [r0]
    subs   r4, r4, r2
    sbc    r5, r5, r3
    strexd r1, r4, r5, [r0]
    cmp    r1, #0
    bne    %b1
    mov    r0, r4
    mov    r1, r5
    dmb    ish
    pop    {r4, r5}
    bx     lr
}

__asm bm_uint64 __sync_val_compare_and_swap_8(bm_uint64* ptr, bm_uint64 oldval, bm_uint64 newval)
{
    push   {r4, r5, r6, r7}
    ldrd   r4, [sp, #16]
    dmb    ish
1
    ldrexd r6, r7, [r0]
    cmp    r7, r3
    cmpeq  r6, r2
    bne    %f2
    strexd r1, r4, r5, [r0]
    cmp    r1, #0
    bne    %b1
2
    mov    r0, r6
    mov    r1, r7
    dmb    ish
    pop    {r4, r5, r6, r7}
    bx     lr
}

__asm bm_bool __sync_bool_compare_and_swap_8(bm_uint64* ptr, bm_uint64 oldval, bm_uint64 newval)
{
    push   {r4, r5, r6, r7}
    ldrd   r4, [sp, #16]
    dmb    ish
1
    ldrexd r6, r7, [r0]
    cmp    r7, r3
    cmpeq  r6, r2
    bne    %f2
    strexd r1, r4, r5, [r0]
    cmp    r1, #0
    bne    %b1
2
    dmb    ish
    moveq  r0, #1
    movne  r0, #0
    pop    {r4, r5, r6, r7}
    bx     lr
}

/*
 * Provide weak definitions of the 4 and 1 atomics
 * in case microlib is used which is missing them.
 */
__attribute__((weak)) __asm bm_uint32 __sync_fetch_and_add_4(bm_uint32* ptr, bm_uint32 val)
{
    dmb    ish
1
    ldrex  r3, [r0]
    add    r2, r3, r1
    strex  ip, r2, [r0]
    cmp    ip, #0
    bne    %b1
    dmb    ish
    mov    r0, r3
    bx     lr
}
__attribute__((weak)) __asm bm_uint32 __sync_sub_and_fetch_4(bm_uint32* ptr, bm_uint32 val)
{
    dmb    ish
1
    ldrex  r3, [r0]
    sub    r3, r3, r1
    strex  r2, r3, [r0]
    cmp    r2, #0
    bne    %b1
    dmb    ish
    mov    r0, r3
    bx     lr
}

__attribute__((weak)) __asm bm_uint32 __sync_val_compare_and_swap_4(bm_uint32* ptr, bm_uint32 oldval, bm_uint32 newval)
{
    dmb    ish
1
    ldrex  r3, [r0]
    cmp    r3, r1
    bne    %f2
    strex  ip, r2, [r0]
    cmp    ip, #0
    bne    %b1
2
    dmb    ish
    mov    r0, r3
    bx     lr
}

__attribute__((weak)) __asm bm_bool __sync_bool_compare_and_swap_4(bm_uint32* ptr, bm_uint32 oldval, bm_uint32 newval)
{
    dmb    ish
1
    ldrex  ip, [r0]
    cmp    ip, r1
    bne    %f2
    strex  r3, r2, [r0]
    cmp    r3, #0
    bne    %b1
2
    dmb    ish
    moveq  r0, #1
    movne  r0, #0
    bx     lr
}

__attribute__((weak)) __asm bm_bool __sync_bool_compare_and_swap_2(bm_uint16* ptr, bm_uint16 oldval, bm_uint16 newval)
{
    dmb    ish
1
    ldrexh ip, [r0]
    cmp    ip, r1
    bne    %f2
    strexh r3, r2, [r0]
    cmp    r3, #0
    bne    %b1
2
    dmb    ish
    moveq  r0, #1
    movne  r0, #0
    bx     lr
}

__attribute__((weak)) __asm bm_uint8 __sync_fetch_and_add_1(bm_uint8* ptr, bm_uint8 val)
{
    dmb    ish
1
    ldrexb r3, [r0]
    add    r2, r3, r1
    strexb ip, r2, [r0]
    cmp    ip, #0
    bne    %b1
    dmb    ish
    uxtb   r0, r3
    bx     lr
}
__attribute__((weak)) __asm bm_uint8 __sync_sub_and_fetch_1(bm_uint8* ptr, bm_uint8 val)
{
    dmb    ish
1
    ldrexb r3, [r0]
    sub    r3, r3, r1
    strexb r2, r3, [r0]
    cmp    r2, #0
    bne    %b1
    dmb    ish
    uxtb   r0, r3
    bx     lr
}

__attribute__((weak)) __asm bm_uint8 __sync_val_compare_and_swap_1(bm_uint8* ptr, bm_uint8 oldval, bm_uint8 newval)
{
    dmb    ish
1
    ldrexb r3, [r0]
    cmp    r3, r1
    bne    %f2
    strexb ip, r2, [r0]
    cmp    ip, #0
    bne    %b1
2
    dmb    ish
    mov    r0, r3
    bx     lr
}
__attribute__((weak)) __asm bm_bool __sync_bool_compare_and_swap_1(bm_uint8* ptr, bm_uint8 oldval, bm_uint8 newval)
{
    dmb    ish
1
    ldrexb ip, [r0]
    cmp    ip, r1
    bne    %f2
    strexb r3, r2, [r0]
    cmp    r3, #0
    bne    %b1
2
    dmb    ish
    moveq  r0, #1
    movne  r0, #0
    bx     lr
}

__attribute__((weak)) __asm void __sync_synchronize(void)
{
    dmb    ish
    bx     lr
}

#pragma pop

#endif
