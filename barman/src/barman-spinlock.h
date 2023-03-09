/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_SPINLOCK
#define INCLUDE_BARMAN_SPINLOCK

#include "barman-types.h"
#include "barman-atomics.h"
#include "multicore/barman-multicore.h"

/**
 * @defgroup    bm_spinlock Spinlock items
 * @brief       Implements a basic spinlock class, that can only be claimed through `try-lock` semantics.
 *              If the lock is already held by the current core it will always fail, otherwise it will spin until
 *              it claims the lock or fails because the core already owns the lock in another thread.
 * @{
 */

/** @brief  Indicates no core owns the spinlock */
#define BM_SPINLOCK_NO_OWNER    (~0u)

/** @brief  Spinlock type */
typedef bm_atomic_uint32 bm_spinlock;

/**
 * @brief   Initialize the lock to unlocked state
 * @param   lock    The lock
 */
BM_NONNULL((1))
static BM_INLINE void barman_spinlock_init(bm_spinlock * lock)
{
    barman_atomic_init(lock, BM_SPINLOCK_NO_OWNER);
}

/**
 * @brief   Try aquire the lock for a named core.
 * @param   lock    The lock
 * @param   core    The core to aqcuire for
 * @return  BM_TRUE if lock was aqcuired, BM_FALSE otherwise
 * @note    Callers should not spin on the result of this function. If the function returns BM_FALSE a failure path
 *          should be taken unless the caller is able to guarentee no deadlock.
 */
BM_NONNULL((1))
static BM_INLINE bm_bool barman_spinlock_trylock_for_core(bm_spinlock * lock, bm_uint32 core)
{
    bm_uint32 previous_owner = barman_atomic_load(lock);

    while (1)
    {
        if ((previous_owner == BM_SPINLOCK_NO_OWNER) && barman_atomic_cmp_ex_strong_pointer(lock, &previous_owner, core)) {
            return BM_TRUE;
        }

        if (previous_owner == core) {
            return BM_FALSE;
        }
        else if (previous_owner != BM_SPINLOCK_NO_OWNER) {
            previous_owner = barman_atomic_load(lock);
        }
    }
}

/**
 * @brief   Try acquire the lock for the current core.
 * @param   lock    The lock
 * @return  The core number as would be returned by {@link barman_get_core_no} on success, or
 *          {@link BM_SPINLOCK_NO_OWNER} on failure.
 */
BM_NONNULL((1))
static BM_INLINE bm_uint32 barman_spinlock_trylock(bm_spinlock * lock)
{
    const bm_uint32 core = barman_get_core_no();

    if (barman_spinlock_trylock_for_core(lock, core)) {
        return core;
    }
    else {
        return BM_SPINLOCK_NO_OWNER;
    }
}

/**
 * @brief   Release the spinlock
 * @param   lock
 */
BM_NONNULL((1))
static BM_INLINE void barman_spinlock_release(bm_spinlock * lock)
{
    barman_atomic_store(lock, BM_SPINLOCK_NO_OWNER);
}

/** @} */

#endif /* INCLUDE_BARMAN_SPINLOCK */
