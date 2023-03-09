/* Copyright (C) 2017-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_DEBUG_CONTROL
#define INCLUDE_BARMAN_DEBUG_CONTROL

#define BM_READ_DEMCR(x)     x = BM_MEMORY_MAPPED_REGISTER_32(0xE000EDFC)
#define BM_WRITE_DEMCR(x)    BM_MEMORY_MAPPED_REGISTER_32(0xE000EDFC) = x

#define BM_DEMCR_TRCENA_BIT      (BM_UINT32(1) << 24)

#endif /* INCLUDE_BARMAN_DEBUG_CONTROL */
