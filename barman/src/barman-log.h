/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_LOG
#define INCLUDE_BARMAN_LOG

#include "barman-types.h"

#if (defined(__GNUC__) || defined(__clang__) || defined(__ARMCC_VERSION))
#pragma GCC system_header
#endif

/* Helpers to enable or disable logging, but retain validation by compiler */
/** @{ */
#if BM_CONFIG_ENABLE_LOGGING
#define BM_LOGGING_LINKAGE          extern
#define BM_LOGGING_BODY             ;
#else
#define BM_LOGGING_LINKAGE          static BM_ALWAYS_INLINE
#define BM_LOGGING_BODY             {}
#endif

#if BM_CONFIG_ENABLE_DEBUG_LOGGING
#define BM_DEBUG_LOGGING_LINKAGE    extern
#define BM_DEBUG_LOGGING_BODY       ;
#else
#define BM_DEBUG_LOGGING_LINKAGE    static BM_ALWAYS_INLINE
#define BM_DEBUG_LOGGING_BODY       {}
#endif

/** @} */

/**
 * @defgroup    bm_log  Debugging facilities
 * @{ */

/**
 * @brief   Print a debug message
 * @param   message
 */
BM_DEBUG_LOGGING_LINKAGE
BM_FORMAT_FUNCTION(printf, 1, 2)
void barman_ext_log_debug(const char * message, ...) BM_DEBUG_LOGGING_BODY

/**
 * @brief   Print an info message
 * @param   message
 */
BM_LOGGING_LINKAGE
BM_FORMAT_FUNCTION(printf, 1, 2)
void barman_ext_log_info(const char * message, ...) BM_LOGGING_BODY

/**
 * @brief   Print a warning message
 * @param   message
 */
BM_LOGGING_LINKAGE
BM_FORMAT_FUNCTION(printf, 1, 2)
void barman_ext_log_warning(const char * message, ...) BM_LOGGING_BODY

/**
 * @brief   Print an error message
 * @param   message
 */
BM_LOGGING_LINKAGE
BM_FORMAT_FUNCTION(printf, 1, 2)
void barman_ext_log_error(const char * message, ...) BM_LOGGING_BODY

/** @brief  Insert a debug message with the file, line number and function name prefixed */
#define BM_DEBUG(message, ...)      barman_ext_log_debug("[%s:%u - %s] " message, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
/** @brief  Insert an info message with the file, line number and function name prefixed */
#define BM_INFO(message, ...)       barman_ext_log_info("[%s:%u - %s] " message, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
/** @brief  Insert a warning message with the file, line number and function name prefixed */
#define BM_WARNING(message, ...)    barman_ext_log_warning("[%s:%u - %s] " message, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
/** @brief  Insert an error message with the file, line number and function name prefixed */
#define BM_ERROR(message, ...)      barman_ext_log_error("[%s:%u - %s] " message, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)

/** @} */

#undef BM_LOGGING_LINKAGE
#undef BM_LOGGING_BODY
#undef BM_DEBUG_LOGGING_LINKAGE
#undef BM_DEBUG_LOGGING_BODY

#endif /* INCLUDE_BARMAN_LOG */
