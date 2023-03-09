/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

/* We define our own to reduce dependency on external headers, and to avoid
 * conflicts with any definitions in other headers where barman headers
 * are included */

#ifndef INCLUDE_BARMAN_TYPES_PUBLIC
#define INCLUDE_BARMAN_TYPES_PUBLIC

/* armclang doesn't always set __GNUC__ when it should */
#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION >= 6000000) && !defined(__GNUC__)
#define __GNUC__ 4
#endif

#include "barman-config.h"

#if defined(__GNUC__)
#pragma GCC system_header
#endif

/**
 * @defgroup    bm_compiler_detection   Compiler detection macros
 * @{ */

/**
 * @def         BM_COMPILER_IS_ARM
 * @brief       Evaluates to true if the compiler is ARMCC or ARMCLANG
 * @def         BM_COMPILER_IS_ARMCC
 * @brief       Evaluates to true if the compiler is ARMCC
 * @def         BM_COMPILER_IS_ARMCLANG
 * @brief       Evaluates to true if the compiler is ARMCLANG
 * @def         BM_COMPILER_IS_CLANG
 * @brief       Evaluates to true if the compiler is CLANG
 * @def         BM_COMPILER_IS_GNUC
 * @brief       Evaluates to true if the compiler is GCC compatible
 *
 * @def         BM_COMPILER_AT_LEAST_ARM
 * @brief       Evaluates to true if the compiler is ARMCC or ARMCLANG of at least the version specified
 * @def         BM_COMPILER_AT_LEAST_ARMCC
 * @brief       Evaluates to true if the compiler is ARMCC of at least the version specified
 * @def         BM_COMPILER_AT_LEAST_ARMCLANG
 * @brief       Evaluates to true if the compiler is ARMCLANG of at least the version specified
 * @def         BM_COMPILER_AT_LEAST_CLANG
 * @brief       Evaluates to true if the compiler is CLANG of at least the version specified
 * @def         BM_COMPILER_AT_LEAST_GNUC
 * @brief       Evaluates to true if the compiler is compatible with GCC of at least the version specified
 */
/** @{ */
#if defined(__ARMCC_VERSION)
#define BM_COMPILER_ARM_MAKE_VERSION(a, b, c)       (((a) * 1000000) + ((b) * 10000) + (c))
#define BM_COMPILER_ARM_VERSION_NO                  (__ARMCC_VERSION)
#define BM_COMPILER_AT_LEAST_ARM(a, b, c)           (BM_COMPILER_ARM_VERSION_NO >= BM_COMPILER_ARM_MAKE_VERSION(a, b, c))
#define BM_COMPILER_IS_ARM                          (1)
#else
#define BM_COMPILER_AT_LEAST_ARM(a, b, c)           (0)
#define BM_COMPILER_IS_ARM                          (0)
#endif

#if BM_COMPILER_IS_ARM && !BM_COMPILER_AT_LEAST_ARM(6,0,0)
#define BM_COMPILER_AT_LEAST_ARMCC(a, b, c)         BM_COMPILER_AT_LEAST_ARM(a, b, c)
#define BM_COMPILER_ARMCC_VERSION_NO                BM_COMPILER_ARM_VERSION_NO
#define BM_COMPILER_IS_ARMCC                        (1)
#else
#define BM_COMPILER_AT_LEAST_ARMCC(a, b, c)         (0)
#define BM_COMPILER_IS_ARMCC                        (0)
#endif

#if BM_COMPILER_IS_ARM && BM_COMPILER_AT_LEAST_ARM(6,0,0)
#define BM_COMPILER_AT_LEAST_ARMCLANG(a, b, c)      BM_COMPILER_AT_LEAST_ARM(a, b, c)
#define BM_COMPILER_ARMCLANG_VERSION_NO             BM_COMPILER_ARM_VERSION_NO
#define BM_COMPILER_IS_ARMCLANG                     (1)
#else
#define BM_COMPILER_AT_LEAST_ARMCLANG(a, b, c)      (0)
#define BM_COMPILER_IS_ARMCLANG                     (0)
#endif

#if defined(__clang__)
#define BM_COMPILER_CLANG_MAKE_VERSION(a, b, c)     (((a) * 0x10000) | (((b) & 0xff) * 0x100) | ((c) & 0xff))
#define BM_COMPILER_CLANG_VERSION_NO                BM_COMPILER_CLANG_MAKE_VERSION(__clang_major__, __clang_minor__, __clang_patchlevel__)
#define BM_COMPILER_AT_LEAST_CLANG(a, b, c)         (BM_COMPILER_CLANG_VERSION_NO >= BM_COMPILER_CLANG_MAKE_VERSION(a, b, c))
#define BM_COMPILER_IS_CLANG                        (1)
#else
#define BM_COMPILER_AT_LEAST_CLANG(a, b, c)         (0)
#define BM_COMPILER_IS_CLANG                        (0)
#endif

#if defined(__GNUC__)
#define BM_COMPILER_GNUC_MAKE_VERSION(a, b, c)      (((a) * 0x10000) | (((b) & 0xff) * 0x100) | ((c) & 0xff))
#define BM_COMPILER_GNUC_VERSION_NO                 BM_COMPILER_GNUC_MAKE_VERSION(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__)
#define BM_COMPILER_AT_LEAST_GNUC(a, b, c)          (BM_COMPILER_GNUC_VERSION_NO >= BM_COMPILER_GNUC_MAKE_VERSION(a, b, c))
#define BM_COMPILER_IS_GNUC                         (1)
#else
#define BM_COMPILER_AT_LEAST_GNUC(a, b, c)          (0)
#define BM_COMPILER_IS_GNUC                         (0)
#endif

#if !(defined(__ARMCC_VERSION) || defined(__clang__) || defined(__GNUC__))
#pragma message ("WARNING: Compiler is not recognized")
#endif
/** @} */

/** @} */

/**
 * @defgroup    bm_target_detection Target detection macros
 * @{ */

/**
 * @def         BM_ARM_TARGET_ARCH
 * @brief       Defines the arm architecture level
 */
#if defined(__aarch64__)
# if defined(__ARM_ARCH)
#   define BM_ARM_TARGET_ARCH  __ARM_ARCH
# else
#   define BM_ARM_TARGET_ARCH  8
# endif
#elif defined(__arm__)
# if defined(__ARM_ARCH)
#   define BM_ARM_TARGET_ARCH  __ARM_ARCH
# elif defined(__TARGET_ARCH_ARM)
#   define BM_ARM_TARGET_ARCH  __TARGET_ARCH_ARM
# elif defined(__ARM_ARCH_7__)
#   define BM_ARM_TARGET_ARCH  7
# elif defined(__ARM_ARCH_6__)
#   define BM_ARM_TARGET_ARCH  6
# else
#   define BM_ARM_TARGET_ARCH  0
# endif
#else
# define BM_ARM_TARGET_ARCH  0
#endif

/**
 * @def         BM_ARM_ARCH_PROFILE
 * @brief       Defines the arm architecture profile
 */
#if defined(__ARM_ARCH_PROFILE)
#  define BM_ARM_ARCH_PROFILE __ARM_ARCH_PROFILE
#elif defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_8A__) || defined(__TARGET_ARCH_7_A)
#  define BM_ARM_ARCH_PROFILE 'A'
#elif defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_8R__) || defined(__TARGET_ARCH_7_R)
#  define BM_ARM_ARCH_PROFILE 'R'
#elif defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_8M_BASE__) || defined(__ARM_ARCH_8M_MAIN__) || defined(__TARGET_ARCH_6_M) || defined(__TARGET_ARCH_7_M) || defined(__TARGET_ARCH_7E_M)
#  define BM_ARM_ARCH_PROFILE 'M'
#endif

/**
 * @def         BM_ARM_ARCH_PROFILE_IS_AR
 * @brief       Defines whether the arm architecture profile includes A and R subset
 */
#if BM_ARM_ARCH_PROFILE == 'A' || BM_ARM_ARCH_PROFILE == 'R' || BM_ARM_ARCH_PROFILE == 'S'
#  define BM_ARM_ARCH_PROFILE_IS_AR 1
#else
#  define BM_ARM_ARCH_PROFILE_IS_AR 0
#endif

/**
 * @def         BM_ARM_64BIT_STATE
 * @brief       Defines whether targeting AArch64
 *
 * @def         BM_ARM_32BIT_STATE
 * @brief       Defines whether targeting AArch32
 */
#if defined(__ARM_64BIT_STATE) || defined(__aarch64__)
#define         BM_ARM_64BIT_STATE 1
#define         BM_ARM_32BIT_STATE 0
#elif defined(__ARM_32BIT_STATE) || defined(__arm__)
#define         BM_ARM_64BIT_STATE 0
#define         BM_ARM_32BIT_STATE 1
#else
#define         BM_ARM_64BIT_STATE 0
#define         BM_ARM_32BIT_STATE 0
#endif

/** Check if the target architecture is ARMv8 */
#define         BM_ARM_TARGET_ARCH_IS_ARMv8()   ((BM_ARM_TARGET_ARCH >= 8) && (BM_ARM_TARGET_ARCH < 900) )
/** Check if the target architecture is ARMv7 */
#define         BM_ARM_TARGET_ARCH_IS_ARMv7()   (BM_ARM_TARGET_ARCH == 7)
/** Check if the target architecture is ARMv6 */
#define         BM_ARM_TARGET_ARCH_IS_ARMv6()   (BM_ARM_TARGET_ARCH == 6)
/** Check if the target architecture is unknown */
#define         BM_ARM_TARGET_ARCH_IS_UNKNOWN() ((BM_ARM_TARGET_ARCH < 6) || (BM_ARM_TARGET_ARCH > 899))

/** @} */


/**
 * @defgroup    bm_integer_types   Basic integer type definitions
 * @{ */
typedef char                bm_bool;    /**< Boolean value */
typedef signed char         bm_int8;    /**< Signed 8-bit value */
typedef unsigned char       bm_uint8;   /**< Unsigned 8-bit value */
typedef signed short        bm_int16;   /**< Signed 16-bit value */
typedef unsigned short      bm_uint16;  /**< Unsigned 16-bit value */
typedef signed int          bm_int32;   /**< Signed 32-bit value */
typedef unsigned int        bm_uint32;  /**< Unsigned 32-bit value */
typedef signed long long    bm_int64;   /**< Signed 64-bit value */
typedef unsigned long long  bm_uint64;  /**< Unsigned 64-bit value */
typedef signed long         bm_intptr;  /**< Signed value of size greater than or equal to a pointer */
typedef unsigned long       bm_uintptr; /**< Unsigned value of size greater than or equal to a pointer */
typedef unsigned long       bm_size_t;  /**< `size_t` type */
/** @} */

/**
 * @defgroup    bm_integer_typed_literals   Basic typed integer literals
 * @{ */
#define BM_INT8(c)    c        /**< Signed 8-bit value */
#define BM_UINT8(c)   c        /**< Unsigned 8-bit value */
#define BM_INT16(c)   c        /**< Signed 16-bit value */
#define BM_UINT16(c)  c        /**< Unsigned 16-bit value */
#define BM_INT32(c)   c        /**< Signed 32-bit value */
#define BM_UINT32(c)  c ## U   /**< Unsigned 32-bit value */
#define BM_INT64(c)   c ## LL  /**< Signed 64-bit value */
#define BM_UINT64(c)  c ## ULL /**< Unsigned 64-bit value */
#define BM_INTPTR(c)  c ## L   /**< Signed value of size greater than or equal to a pointer */
#define BM_UINTPTR(c) c ## UL  /**< Unsigned value of size greater than or equal to a pointer */
/** @} */

/**
 * @defgroup    bm_constant_defines Generic constant values
 * @{
 */
#define BM_FALSE            ((bm_bool) 0)   /**< False value */
#define BM_TRUE             (!BM_FALSE)     /**< True value */
#define BM_NULL             0               /**< Null value */
/** @} */

/**
 * @defgroup    bm_utility_macros   Utility macros
 * @{
 */

/**
 * @def         BM_COUNT_OF
 * @brief       Returns the number of items in a fixed size array
 */
#define BM_COUNT_OF(a)      (sizeof(a) / sizeof(a[0]))

/**
 * @def         asm
 * @brief       Allow the asm keyword in ANSI C89 for compilers that support it
 */
#ifndef asm
#if BM_COMPILER_IS_CLANG || BM_COMPILER_IS_GNUC
#ifdef __STRICT_ANSI__
#define asm                 __asm
#endif
#elif BM_COMPILER_IS_ARMCC
#define asm                 __asm
#else
#pragma message ("WARNING: asm is not defined on this compiler")
#endif
#endif

/**
 * @def         BM_INLINE
 * @brief       Allow the use of the inline keyword even in ANSI C89 for compilers that support it
 */
#if (BM_COMPILER_IS_CLANG || BM_COMPILER_IS_GNUC)
#define BM_INLINE           __inline
#elif (BM_COMPILER_IS_ARMCC || BM_COMPILER_IS_ARMCLANG)
#define BM_INLINE           __inline
#else
#define BM_INLINE
#endif

/**
 * @def         BM_ALWAYS_INLINE
 * @brief       Force a function to be always inlined
 */
#if (BM_COMPILER_IS_CLANG || BM_COMPILER_IS_GNUC)
#define BM_ALWAYS_INLINE    __attribute__((always_inline)) __inline
#elif (BM_COMPILER_IS_ARMCC || BM_COMPILER_IS_ARMCLANG)
#define BM_ALWAYS_INLINE    __attribute__((always_inline)) __inline
#else
#define BM_ALWAYS_INLINE
#endif

/**
 * @def         BM_NEVER_INLINE
 * @brief       Force a function to be never inlined
 */
#if defined(noinline)
/* U-Boot, for example, defines its own noinline macro that collides here */
#define BM_NEVER_INLINE     noinline
#elif (BM_COMPILER_IS_CLANG || BM_COMPILER_IS_GNUC)
#define BM_NEVER_INLINE     __attribute__((noinline))
#elif (BM_COMPILER_IS_ARMCC || BM_COMPILER_IS_ARMCLANG)
#define BM_NEVER_INLINE     __attribute__((noinline))
#else
#define BM_NEVER_INLINE
#endif

/**
 * @def         BM_NONNULL
 * @brief       Defines the __attribute__((nonnull)); allows some compiler checking for non-null values
 */
#if (BM_COMPILER_IS_CLANG || BM_COMPILER_IS_GNUC || BM_COMPILER_IS_ARMCC || BM_COMPILER_IS_ARMCLANG)
#define BM_NONNULL(x)       __attribute__((nonnull x))
#else
#define BM_NONNULL(x)
#endif

/**
 * @def         BM_RET_NONNULL
 * @brief       Defines the __attribute__((returns_nonnull)); allows compiler to know return value is non-null
 */
#if (BM_COMPILER_AT_LEAST_CLANG(3, 5, 0) || BM_COMPILER_AT_LEAST_GNUC(4, 9, 0))
#define BM_RET_NONNULL      __attribute__((returns_nonnull))
#else
#define BM_RET_NONNULL
#endif

/**
 * @def         BM_WEAK
 * @brief       Labels the function weak linkage
 */
#if (BM_COMPILER_IS_CLANG || BM_COMPILER_IS_GNUC || BM_COMPILER_IS_ARMCC || BM_COMPILER_IS_ARMCLANG)
#define BM_WEAK             __attribute__((weak))
#else
#pragma message ("WARNING: BM_WEAK is not defined on this compiler")
#define BM_WEAK
#endif

/**
 * @def         BM_PACKED_TYPE
 * @brief       Labels the function weak linkage
 */
#if (BM_COMPILER_IS_CLANG || BM_COMPILER_IS_GNUC || BM_COMPILER_IS_ARMCC || BM_COMPILER_IS_ARMCLANG)
#define BM_PACKED_TYPE      __attribute__((packed))
#else
#pragma message ("WARNING: BM_PACKED_TYPE is not defined on this compiler")
#define BM_PACKED_TYPE
#endif


/**
 * @def         BM_FORMAT_FUNCTION
 * @brief       Labels the function as having printf or similar format parameters. Compilers that support this attribute will check the
 *              parameters are sane
 */
#if (BM_COMPILER_IS_CLANG || BM_COMPILER_IS_GNUC || BM_COMPILER_IS_ARMCC || BM_COMPILER_IS_ARMCLANG)
#define BM_FORMAT_FUNCTION(archetype, string_index, first_to_check)      __attribute__((format(archetype, string_index, first_to_check)))
#else
#pragma message ("WARNING: BM_FORMAT_FUNCTION is not defined on this compiler")
#define BM_FORMAT_FUNCTION(archetype, string_index, first_to_check)
#endif

/** @} */

#endif /* INCLUDE_BARMAN_TYPES_PUBLIC */
