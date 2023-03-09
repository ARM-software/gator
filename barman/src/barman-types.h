/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

/* We define our own to reduce dependency on external headers, and to avoid
 * conflicts with any definitions in other headers where barman headers
 * are included */

#ifndef INCLUDE_BARMAN_TYPES
#define INCLUDE_BARMAN_TYPES

#include "barman-types-public.h"


/**
 * @defgroup    bm_utility_macros   Utility macros
 * @{
 */

/**
 * @def         BM_BIG_ENDIAN
 */
#if defined(__ARM_BIG_ENDIAN) || defined(__BIG_ENDIAN) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define BM_BIG_ENDIAN       (1)
#endif

/**
 * @def         BM_BIT
 * @brief       Sets the `b`th bit in an integer value
 */
#define BM_BIT(b)           (1ul << (b))

/**
 * @def         BM_GET_BYTE
 * @brief       Gets the `b`th byte in an integer value
 */
#define BM_GET_BYTE(byte, value)           (0xff & ((value) >> 8 * (byte)))

/**
 * @def         BM_SWAP_ENDIANESS_32
 * @brief       Swaps the endianess of a 32 bit integer
 */
#define BM_SWAP_ENDIANESS_32(value)        (BM_GET_BYTE(0, value) << 24 | BM_GET_BYTE(1, value) << 16 | BM_GET_BYTE(2, value) << 8 | BM_GET_BYTE(3, value))

/**
 * @def         BM_MIN
 * @brief       Returns the result of (a < b ? a : b)
 */
#define BM_MIN(a, b)        ((a) < (b) ? (a) : (b))

/**
 * @def         BM_MAX
 * @brief       Returns the result of (a > b ? a : b)
 */
#define BM_MAX(a, b)        ((a) > (b) ? (a) : (b))

/**
 * @def         BM_STRINGIFY_TOK
 * @brief       The parameter token(s) are converted into a string
 */
#define BM_STRINGIFY_TOK(x) #x

/**
 * @def         BM_STRINGIFY_VAL
 * @brief       The value of the parameter is converted into a string
 */
#define BM_STRINGIFY_VAL(x) BM_STRINGIFY_TOK(x)

/**
 * @def         BM_UNALIGNED_POINTER_TYPE
 * @brief       Makes type type into an unaligned pointer type
 */
#if (BM_COMPILER_IS_CLANG || BM_COMPILER_IS_GNUC || BM_COMPILER_IS_ARMCC || BM_COMPILER_IS_ARMCLANG)
#define BM_UNALIGNED_POINTER_TYPE(type)     type __attribute__((aligned(1)))  *
#else
#pragma message ("WARNING: BM_UNALIGNED_POINTER_TYPE is not defined on this compiler")
#define BM_UNALIGNED_POINTER_TYPE(type)     type *
#endif

/**
 * @def         BM_ALIGN
 * @brief       Force alignment attribute
 */
#if (BM_COMPILER_IS_GNUC)
#define BM_ALIGN(alignment)     __attribute__((aligned(alignment)))
#else
#pragma message ("WARNING: BM_ALIGN is not defined on this compiler")
#define BM_ALIGN(alignment)
#endif

/**
 * @def         BM_ASSUME_ALIGNED
 * @brief       This function returns its first argument,
 *              and allows the compiler to assume that the returned pointer is at least alignment bytes aligned
 */
#if (BM_COMPILER_IS_GNUC && !BM_COMPILER_IS_ARMCC )
#define BM_ASSUME_ALIGNED(pointer, alignment)     __builtin_assume_aligned(pointer, alignment)
#else
#define BM_ASSUME_ALIGNED(pointer, alignment)     (pointer)
#if !BM_COMPILER_IS_ARMCC
#pragma message ("WARNING: BM_ASSUME_ALIGNED is not defined on this compiler, -Wcast-align may be triggered")
#endif
#endif

/**
 * @def         BM_ASSUME_ALIGNED_CAST
 * @brief       This function returns its second argument cast a pointer to type,
 *              and allows the compiler to assume that the returned pointer is at least aligned the size of type
 */
#define BM_ASSUME_ALIGNED_CAST(type, pointer)     ((type * ) (bm_uintptr)(pointer))

/**
 * @def         BM_UNALIGNED_CAST_DEREF_ASSIGN
 * @brief       Casts pointer to pointer to unaligned type, dereferences it and assigns value to it
 */
#if (BM_COMPILER_IS_GNUC)
#define BM_UNALIGNED_CAST_DEREF_ASSIGN(type, pointer, value)                \
    do {                                                                    \
        struct {type field __attribute__((packed));} * _bm_struct_ptr;      \
        _bm_struct_ptr = __extension__ (__typeof(_bm_struct_ptr)) pointer;  \
        _bm_struct_ptr->field = value;                                      \
    } while (0)
#else
#pragma message ("WARNING: BM_UNALIGNED_CAST_DEREF_ASSIGN is not defined on this compiler")
#define BM_UNALIGNED_CAST_DEREF_ASSIGN(type, pointer, value)     do {*((type *) (pointer)) = value; } while (0)
#endif

/**
 * @def         BM_C_ASSERT
 * @brief       Encodes an expression that evaluates to the value of `res`, but for compilers that support it
 *              will use whatever means to encode a statement that will cause the compiler to fail if `test`
 *              evaluates at compile time to false.
 *              `token` must be a identifier token that is inserted into the generated expression to help identify the
 *              failure.
 */
#if (BM_COMPILER_IS_CLANG || BM_COMPILER_IS_GNUC)
#define BM_C_ASSERT(test, token, res)       ({ enum {token = 1 / ((test) ? 1 : 0)}; (res); })
#else
#define BM_C_ASSERT(test, token, res)       (res)
#endif

/**
 * @def         BM_MEMORY_MAPPED_REGISTER
 * @brief       Creates a memory mapped register l-value at `address` of type `size_type`
 */
#define BM_MEMORY_MAPPED_REGISTER(address, size_type)    (*BM_ASSUME_ALIGNED_CAST(volatile size_type, ((void*) (address))))
#define BM_MEMORY_MAPPED_REGISTER_8(address)             BM_MEMORY_MAPPED_REGISTER(address, bm_uint8)
#define BM_MEMORY_MAPPED_REGISTER_16(address)            BM_MEMORY_MAPPED_REGISTER(address, bm_uint16)
#define BM_MEMORY_MAPPED_REGISTER_32(address)            BM_MEMORY_MAPPED_REGISTER(address, bm_uint32)
#define BM_MEMORY_MAPPED_REGISTER_64(address)            BM_MEMORY_MAPPED_REGISTER(address, bm_uint64)


/**
 * @brief The unit type which only has one value
 */
typedef enum { BM_UNIT_TYPE_VALUE = 1 } bm_unit_type;

/** @} */

#endif /* INCLUDE_BARMAN_TYPES */
