/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef INCLUDE_BARMAN_ASSEMBLY
#define INCLUDE_BARMAN_ASSEMBLY

#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION < 6000000)
/* Arm syntax */

#undef BM_ESCAPE
#define BM_ESCAPE(name) ||name||

#define BM_BEGIN_FUNC(name)               name PROC
#define BM_END_FUNC(name)                 ENDP
#define BM_LABEL(name)                    name
#define BM_END                            END
#define BM_GLOBAL(name)                   GLOBAL name
#define BM_EXTERN(name)                   EXTERN name
#define BM_SECTION_X(name, p2align)       AREA |name|, CODE, READONLY, ALIGN=p2align
#define BM_BYTE_ALIGN(alignment)          ALIGN alignment
#define BM_ARM                            ARM
#define BM_THUMB                          THUMB
#define BM_PRESERVE8                      PRESERVE8

#else
/* GNU syntax */

    .macro  BM_ASM_BEGIN_FUNC name
    .type \name, "function"
\name:
    .endm

    .macro BM_ASM_SECTION_X name p2align
    .section \name, "x"
    .p2align \p2align
    .endm

#define BM_BEGIN_FUNC(name)               BM_ASM_BEGIN_FUNC name
#define BM_END_FUNC(name)                 .size name, . - name
#define BM_LABEL(name)                    name:
#define BM_END                            .end
#define BM_GLOBAL(name)                   .global name
#define BM_EXTERN(name)                   .extern name
#define BM_SECTION_X(name, p2align)       BM_ASM_SECTION_X name, p2align
#define BM_BYTE_ALIGN(alignment)          .balign alignment
#define BM_ARM                            .arm
#define BM_THUMB                          .thumb
#define BM_PRESERVE8                      .eabi_attribute 25, 1

#endif

#endif /* INCLUDE_BARMAN_ASSEMBLY */
