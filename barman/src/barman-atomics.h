/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_ATOMICS
#define INCLUDE_BARMAN_ATOMICS

#include "barman-types.h"

#if defined(__GNUC__)
#pragma GCC system_header
#endif

/**
 * @defgroup    bm_atomics  Atomic operations
 * @{
 *
 * @def     BM_ATOMIC_TYPE
 * @brief   Marks some type as being atomic as if the C11 _Atomic modifier were applied
 *
 * @def     BM_ATOMIC_VAR_INIT
 * @brief   Defined to match the C11 expression `ATOMIC_VAR_INIT(var)`
 *
 * @def     barman_atomic_init
 * @brief   Defined to match the C11 expression `atomic_init(pointer)`
 *
 * @def     barman_atomic_is_lock_free
 * @brief   Defined to match the C11 expression `atomic_is_lock_free(pointer)`
 *
 * @def     barman_atomic_store
 * @brief   Defined to match the C11 expression `atomic_store_explicit(pointer, val, memory_order_release)`
 *
 * @def     barman_atomic_load
 * @brief   Defined to match the C11 expression `atomic_load_explicit(pointer, memory_order_acquire)`
 *
 * @def     barman_atomic_exchange
 * @brief   Defined to match the C11 expression `atomic_exchange_explicit(pointer, val, memory_order_acq_rel)`
 *
 * @def     barman_atomic_cmp_ex_strong_value
 * @brief   Defined to match the C11 expression `atomic_compare_exchange_strong_explicit(pointer, oldval, newval, memory_order_acq_rel, memory_order_relaxed)`
 *          except that the second parameter (`oldval`) is a value rather than a pointer to a value
 *
 * @def     barman_atomic_cmp_ex_weak_value
 * @brief   Defined to match the C11 expression `atomic_compare_exchange_weak_explicit(pointer, oldval, newval, memory_order_acq_rel, memory_order_relaxed)`
 *          except that the second parameter (`oldval`) is a value rather than a pointer to a value
 *
 * @def     barman_atomic_cmp_ex_strong_pointer
 * @brief   Defined to match the C11 expression `atomic_compare_exchange_strong_explicit(pointer, expected, newval, memory_order_acq_rel, memory_order_relaxed)`
 *
 * @def     barman_atomic_cmp_ex_weak_pointer
 * @brief   Defined to match the C11 expression `atomic_compare_exchange_weak_explicit(pointer, expected, newval, memory_order_acq_rel, memory_order_relaxed)`
 *
 * @def     barman_atomic_fetch_add
 * @brief   Defined to match the C11 expression `atomic_fetch_add_explicit(pointer, val, memory_order_acq_rel)`
 *
 * @def     barman_atomic_fetch_sub
 * @brief   Defined to match the C11 expression `atomic_fetch_sub_explicit(pointer, val, memory_order_acq_rel)`
 *
 * @def     barman_atomic_fetch_and
 * @brief   Defined to match the C11 expression `atomic_fetch_and_explicit(pointer, val, memory_order_acq_rel)`
 *
 * @def     barman_atomic_fetch_or
 * @brief   Defined to match the C11 expression `atomic_fetch_or_explicit(pointer, val, memory_order_acq_rel)`
 *
 * @def     barman_atomic_fetch_xor
 * @brief   Defined to match the C11 expression `atomic_fetch_xor_explicit(pointer, val, memory_order_acq_rel)`
 *
 * @def     barman_atomic_add_fetch
 * @brief   Defined to match the C11 expression `atomic_fetch_add_explicit(pointer, val, memory_order_acq_rel) + val`
 *
 * @def     barman_atomic_sub_fetch
 * @brief   Defined to match the C11 expression `atomic_fetch_sub_explicit(pointer, val, memory_order_acq_rel) - val`
 *
 * @def     barman_atomic_and_fetch
 * @brief   Defined to match the C11 expression `atomic_fetch_and_explicit(pointer, val, memory_order_acq_rel) & val`
 *
 * @def     barman_atomic_or_fetch
 * @brief   Defined to match the C11 expression `atomic_fetch_or_explicit(pointer, val, memory_order_acq_rel) | val`
 *
 * @def     barman_atomic_xor_fetch
 * @brief   Defined to match the C11 expression `atomic_fetch_xor_explicit(pointer, val, memory_order_acq_rel) ^ val`
 */

#if (BM_COMPILER_AT_LEAST_GNUC(4, 7, 0) && !BM_COMPILER_IS_ARMCC) || BM_COMPILER_AT_LEAST_CLANG(3, 4, 0)

#define BM_ATOMIC_TYPE(type)                    type
#define BM_ATOMIC_VAR_INIT(val)                 (val)
#define barman_atomic_init(pointer, val)        __atomic_store_n((pointer), (val), __ATOMIC_RELAXED)
#define barman_atomic_is_lock_free(pointer)                         \
    (__extension__                                                  \
    ({  __typeof(pointer) _bma_tmp_pointer_ = (pointer);            \
        __atomic_always_lock_free(sizeof(*_bma_tmp_pointer_),       \
                                  _bma_tmp_pointer_);               \
    }))
#define barman_atomic_store(pointer, val)       __atomic_store_n((pointer), (val), __ATOMIC_RELEASE)
#define barman_atomic_load(pointer)             __atomic_load_n((pointer), __ATOMIC_ACQUIRE)
#define barman_atomic_exchange(pointer, val)    __atomic_exchange_n((pointer), (val), __ATOMIC_ACQ_REL)
#define barman_atomic_cmp_ex_strong_value(pointer, oldval, newval)  \
    (__extension__                                                  \
    ({  __typeof(pointer) _bma_tmp_pointer_ = (pointer);            \
        __typeof(*_bma_tmp_pointer_) _bma_tmp_oldval_ = (oldval);   \
        __atomic_compare_exchange_n(_bma_tmp_pointer_,              \
                                    &_bma_tmp_oldval_,              \
                                    (newval),                       \
                                    BM_FALSE,                       \
                                    __ATOMIC_ACQ_REL,               \
                                    __ATOMIC_RELAXED); }))
#define barman_atomic_cmp_ex_weak_value(pointer, oldval, newval)    \
    (__extension__                                                  \
    ({  __typeof(pointer) _bma_tmp_pointer_ = (pointer);            \
        __typeof(*_bma_tmp_pointer_) _bma_tmp_oldval_ = (oldval);   \
        __atomic_compare_exchange_n(_bma_tmp_pointer_,              \
                                    &_bma_tmp_oldval_,              \
                                    (newval),                       \
                                    BM_TRUE,                        \
                                    __ATOMIC_ACQ_REL,               \
                                    __ATOMIC_RELAXED); }))
#define barman_atomic_cmp_ex_strong_pointer(pointer, expected, newval)  \
        __atomic_compare_exchange_n((pointer), (expected), (newval), BM_FALSE, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)
#define barman_atomic_cmp_ex_weak_pointer(pointer, expected, newval)    \
        __atomic_compare_exchange_n((pointer), (expected), (newval), BM_TRUE, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)
#define barman_atomic_fetch_add(pointer, val)   __atomic_fetch_add((pointer), (val), __ATOMIC_ACQ_REL)
#define barman_atomic_fetch_sub(pointer, val)   __atomic_fetch_sub((pointer), (val), __ATOMIC_ACQ_REL)
#define barman_atomic_fetch_and(pointer, val)   __atomic_fetch_and((pointer), (val), __ATOMIC_ACQ_REL)
#define barman_atomic_fetch_or(pointer, val)    __atomic_fetch_or((pointer), (val), __ATOMIC_ACQ_REL)
#define barman_atomic_fetch_xor(pointer, val)   __atomic_fetch_xor((pointer), (val), __ATOMIC_ACQ_REL)
#define barman_atomic_add_fetch(pointer, val)   __atomic_add_fetch((pointer), (val), __ATOMIC_ACQ_REL)
#define barman_atomic_sub_fetch(pointer, val)   __atomic_sub_fetch((pointer), (val), __ATOMIC_ACQ_REL)
#define barman_atomic_and_fetch(pointer, val)   __atomic_and_fetch((pointer), (val), __ATOMIC_ACQ_REL)
#define barman_atomic_or_fetch(pointer, val)    __atomic_or_fetch((pointer), (val), __ATOMIC_ACQ_REL)
#define barman_atomic_xor_fetch(pointer, val)   __atomic_xor_fetch((pointer), (val), __ATOMIC_ACQ_REL)

#elif (BM_COMPILER_AT_LEAST_GNUC(4, 4, 0) || BM_COMPILER_AT_LEAST_ARMCC(5, 0, 0))

#if (BM_ARM_TARGET_ARCH > 6)
#define BM_ATOMIC_TYPE(type)                    type __attribute__((aligned(sizeof(type))))
#else
#define BM_ATOMIC_TYPE(type)                    type __attribute__((aligned(sizeof(void *))))
#endif

#define BM_ATOMIC_VAR_INIT(val)                 (val)
#define barman_atomic_init(pointer, val)        do { *(pointer) = (val); } while (0)
#define barman_atomic_is_lock_free(pointer)                                         \
    (__extension__                                                                  \
    (((BM_ARM_TARGET_ARCH > 6) && (__alignof__(*(pointer)) == sizeof(*(pointer))))  \
     || ((BM_ARM_TARGET_ARCH == 6) && (__alignof__(*(pointer)) == 4))))
#define barman_atomic_store(pointer, val)       do { __sync_synchronize(); *(pointer) = (val); } while (0)
#define barman_atomic_load(pointer)                         \
    (__extension__                                          \
    ({                                                      \
        __typeof(*(pointer)) _bma_tmp_val_ = *(pointer);    \
        __sync_synchronize();                               \
        (_bma_tmp_val_);                                    \
    }))
#define barman_atomic_exchange(pointer, val)                                            \
    (__extension__                                                                      \
    ({                                                                                  \
        volatile __typeof(pointer) _bma_tmp_pointer_ = (pointer);                       \
        __sync_synchronize();                                                           \
        __typeof(*_bma_tmp_pointer_) _bma_tmp_val_ = *_bma_tmp_pointer_;                \
        while (!__sync_bool_compare_and_swap(_bma_tmp_pointer_, _bma_tmp_val_, (val)))  \
        {                                                                               \
            _bma_tmp_val_ = *_bma_tmp_pointer_;                                         \
        }                                                                               \
        (_bma_tmp_val_);                                                                \
    }))
#define barman_atomic_cmp_ex_strong_value(pointer, oldval, newval)  \
    __sync_bool_compare_and_swap((pointer), (oldval), (newval))
#define barman_atomic_cmp_ex_weak_value(pointer, oldval, newval)    \
    barman_atomic_cmp_ex_strong_value((pointer), (oldval), (newval))
#define barman_atomic_cmp_ex_strong_pointer(pointer, expected, newval)                                                      \
    (__extension__                                                                                                          \
    ({                                                                                                                      \
        __typeof(*(pointer)) _bma_tmp_expected_val_ = *(expected);                                                          \
        __typeof(*(pointer)) _bma_tmp_result_ = __sync_val_compare_and_swap((pointer), _bma_tmp_expected_val_, (newval));   \
        *(expected) = _bma_tmp_result_;                                                                                     \
        (_bma_tmp_expected_val_ == _bma_tmp_result_);                                                                       \
    }))
#define barman_atomic_cmp_ex_weak_pointer(pointer, expected, newval)        \
    barman_atomic_cmp_ex_strong_pointer((pointer), (expected), (newval))
#define barman_atomic_fetch_add(pointer, val)   __sync_fetch_and_add((pointer), (val))
#define barman_atomic_fetch_sub(pointer, val)   __sync_fetch_and_sub((pointer), (val))
#define barman_atomic_fetch_and(pointer, val)   __sync_fetch_and_and((pointer), (val))
#define barman_atomic_fetch_or(pointer, val)    __sync_fetch_and_or((pointer), (val))
#define barman_atomic_fetch_xor(pointer, val)   __sync_fetch_and_xor((pointer), (val))
#define barman_atomic_add_fetch(pointer, val)   __sync_add_and_fetch((pointer), (val))
#define barman_atomic_sub_fetch(pointer, val)   __sync_sub_and_fetch((pointer), (val))
#define barman_atomic_and_fetch(pointer, val)   __sync_and_and_fetch((pointer), (val))
#define barman_atomic_or_fetch(pointer, val)    __sync_or_and_fetch((pointer), (val))
#define barman_atomic_xor_fetch(pointer, val)   __sync_xor_and_fetch((pointer), (val))

#else

#error "Unsupported compiler version. Atomic operations not implemented."

#define BM_ATOMIC_TYPE(type)
#define BM_ATOMIC_VAR_INIT(val)
#define barman_atomic_init(pointer, val)
#define barman_atomic_is_lock_free(pointer)
#define barman_atomic_store(pointer, val)
#define barman_atomic_load(pointer)
#define barman_atomic_exchange(pointer, val)
#define barman_atomic_cmp_ex_strong_value(pointer, oldval, newval)
#define barman_atomic_cmp_ex_weak_value(pointer, oldval, newval)
#define barman_atomic_cmp_ex_strong_pointer(pointer, expected, newval)
#define barman_atomic_cmp_ex_weak_pointer(pointer, expected, newval)
#define barman_atomic_fetch_add(pointer, val)
#define barman_atomic_fetch_sub(pointer, val)
#define barman_atomic_fetch_and(pointer, val)
#define barman_atomic_fetch_or(pointer, val)
#define barman_atomic_fetch_xor(pointer, val)
#define barman_atomic_add_fetch(pointer, val)
#define barman_atomic_sub_fetch(pointer, val)
#define barman_atomic_and_fetch(pointer, val)
#define barman_atomic_or_fetch(pointer, val)
#define barman_atomic_xor_fetch(pointer, val)

#endif

/* Atomic types */
typedef BM_ATOMIC_TYPE(bm_bool) bm_atomic_bool;         /**< Atomic bm_bool */
typedef BM_ATOMIC_TYPE(bm_int8) bm_atomic_int8;         /**< Atomic bm_int8 */
typedef BM_ATOMIC_TYPE(bm_int16) bm_atomic_int16;       /**< Atomic bm_int16 */
typedef BM_ATOMIC_TYPE(bm_int32) bm_atomic_int32;       /**< Atomic bm_int32 */
typedef BM_ATOMIC_TYPE(bm_int64) bm_atomic_int64;       /**< Atomic bm_int64 */
typedef BM_ATOMIC_TYPE(bm_intptr) bm_atomic_intptr;     /**< Atomic bm_intptr */
typedef BM_ATOMIC_TYPE(bm_uint8) bm_atomic_uint8;       /**< Atomic bm_uint8 */
typedef BM_ATOMIC_TYPE(bm_uint16) bm_atomic_uint16;     /**< Atomic bm_uint16 */
typedef BM_ATOMIC_TYPE(bm_uint32) bm_atomic_uint32;     /**< Atomic bm_uint32 */
typedef BM_ATOMIC_TYPE(bm_uint64) bm_atomic_uint64;     /**< Atomic bm_uint64 */
typedef BM_ATOMIC_TYPE(bm_uintptr) bm_atomic_uintptr;   /**< Atomic bm_uintptr */

/** @} */

#endif /* INCLUDE_BARMAN_ATOMICS */
