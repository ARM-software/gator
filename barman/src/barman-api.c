/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#include "barman-api.h"
#include "barman-atomics.h"
#include "barman-external-dependencies.h"
#include "barman-intrinsics.h"
#include "barman-protocol.h"
#include "barman-public-functions.h"
#include "barman-log.h"
#include "barman-types.h"
#include "barman-custom-counter-definitions.h"
#include "multicore/barman-multicore.h"
#include "pmu/barman-select-pmu.h"

/* ********************************** */
#if (BM_ARM_ARCH_PROFILE == 'M') && (BM_CONFIG_MIN_SAMPLE_PERIOD > 0)
#error "BM_CONFIG_MIN_SAMPLE_PERIOD is not supported on M profile"
#endif

#if BM_CONFIG_USER_SUPPLIED_PMU_DRIVER
extern bm_uint32 barman_ext_midr(void);
extern bm_uintptr barman_ext_mpidr(void);
#endif

/**
 * @defgroup    bm_public_internals Public function internals
 * @{ */

/** Make sure the size of temporary array is at least one */
#define BM_CUSTOM_COUNTER_ARRAY_SIZE    (BM_NUM_CUSTOM_COUNTERS > 0 ? BM_NUM_CUSTOM_COUNTERS : 1)

/** Mask for bits that are used to construct CPUID values */
#define BM_MIDR_CPUID_MASK 0xff00fff0

/**
 * @brief   Target state for per-core PMU config
 */
enum bm_pmu_target_state
{
    BM_PMU_STATE_UNINITIALIZED = 0, /**< The PMU is not initialized */
    BM_PMU_STATE_SHOULD_START,      /**< The PMU should start on the next sample */
    BM_PMU_STATE_SHOULD_STOP,       /**< The PMU should stop on the next sample */
    BM_PMU_STATE_STARTED,           /**< The PMU is started */
    BM_PMU_STATE_STOPPED            /**< The PMU is stopped */
};

/** Atomic version of {@link bm_pmu_target_state} */
typedef BM_ATOMIC_TYPE(enum bm_pmu_target_state) bm_atomic_pmu_target_state;

/**
 * @brief   PMU initialization state for a PMU
 */
enum bm_pmu_init_state
{
    BM_PMU_INIT_UNINITIALIZED = 0,  /**< The PMU is uninitialized */
    BM_PMU_INIT_INITIALIZING,       /**< The PMU is initializing */
    BM_PMU_INIT_INITIALIZED         /**< The PMU is initialized */
};

/** Atomic version of {@link bm_pmu_init_state} */
typedef BM_ATOMIC_TYPE(enum bm_pmu_init_state) bm_atomic_pmu_init_state;

/** Try change state result */
enum bm_try_change_pmu_result
{
    BM_PMU_STATE_CHANGE_FAILED,     /**< State change failed */
    BM_PMU_STATE_CHANGE_SUCCEEDED,  /**< State change succeeded */
    BM_PMU_STATE_CHANGE_RETRY       /**< State change must be retried */
};

/**
 * @brief   API PMU family settings
 */
struct bm_pmu_family_settings
{
    /** The MIDR to match */
    bm_uint32 midr;
    /** The number of events in event_types */
    bm_uint32 num_events;
    /** The event types to configure */
    bm_uint32 event_types[BM_MAX_PMU_COUNTERS];
    /** The allowed cores bitmap */
    bm_core_set allowed_cores;
};

/**
 * @brief   API per-core settings
 */
struct bm_per_core_settings
{
    /** The number of counters that can be polled */
    bm_uint32 num_counters;
    /** The init state of the PMU */
    bm_atomic_pmu_init_state init_state;
    /** The state of the core */
    bm_atomic_pmu_target_state state;
    /** The PMU family configured for the core */
    bm_uint32 pmu_family;
#if BM_CONFIG_MIN_SAMPLE_PERIOD > 0
    /** The last successful sample timestamp */
    bm_atomic_uint64 last_sample_timestamp;
#endif
};

#if BM_CONFIG_NUM_PMU_TYPES <= 0
#error "Invalid value for BM_CONFIG_NUM_PMU_TYPES"
#endif

/**
 * @brief   API configuration settings
 */
struct bm_settings
{
    /** Number of items stored in pmu_family_settings */
    bm_uint32 num_pmu_family_settings;
    /** The PMU family settings (assumes at most one per core) */
    struct bm_pmu_family_settings pmu_family_settings[BM_CONFIG_NUM_PMU_TYPES];
    /** Per core settings */
    struct bm_per_core_settings per_core_settings[BM_CONFIG_MAX_CORES];
#if BM_CONFIG_MIN_SAMPLE_PERIOD > 0
    /** The minimum_sample_period in the same units as returned by {@link barman_ext_get_timestamp} */
    bm_uint64 minimum_sample_period;
#endif
    /** First 'start' has happened */
    bm_atomic_bool started;
};

/** API settings */
static struct bm_settings bm_settings;

/**
 * @def     BM_GET_RETURN_ADDRESS
 * @brief   Get the return address of a function
 */
#if ((!BM_COMPILER_IS_ARMCC && BM_COMPILER_AT_LEAST_GNUC(4, 5, 0)) || BM_COMPILER_IS_CLANG)
#define BM_GET_RETURN_ADDRESS()         __builtin_extract_return_addr(__builtin_return_address(0))
#elif (BM_COMPILER_IS_GNUC || BM_COMPILER_IS_ARMCC)
#define BM_GET_RETURN_ADDRESS()         __builtin_return_address(0)
#else
#pragma message ("WARNING: BM_GET_RETURN_ADDRESS() was not defined for this compiler")
#define BM_GET_RETURN_ADDRESS()         BM_NULL
#endif

/* ********************************** */

/**
 * @brief   Initialize a core's PMU
 * @param   core    The core to initialize
 * @return  BM_TRUE if successful, BM_FALSE otherwise
 */
static BM_INLINE enum bm_try_change_pmu_result barman_initialize_pmu(bm_uint32 core)
{
    const bm_uint32 midr = barman_midr();
    const bm_uintptr mpidr = barman_mpidr();
    bm_uint32 index, num_counters;
    bm_int32 num_counters_setup;
    bm_bool found_match = BM_FALSE;
    enum bm_pmu_init_state init_state;

#if BM_PMU_HAS_FIXED_CYCLE_COUNTER
    bm_uint32 counter_types[BM_MAX_PMU_COUNTERS] = {0};
    bm_uint32 counter;
#endif

#if BM_MAX_PMU_COUNTERS <= 0
#error "BM_MAX_PMU_COUNTERS is invalid"
#endif

    /* examine current state; do not initialize if busy or if already initialized */
    init_state = barman_atomic_load(&bm_settings.per_core_settings[core].init_state);
    do {
        switch (init_state)
        {
            case BM_PMU_INIT_UNINITIALIZED: {
                break;
            }
            case BM_PMU_INIT_INITIALIZING: {
                return BM_PMU_STATE_CHANGE_RETRY;
            }
            case BM_PMU_INIT_INITIALIZED: {
                return BM_PMU_STATE_CHANGE_SUCCEEDED;
            }
            default: {
                BM_DEBUG("Unexpected value for init_state: %i\n", init_state);
                return BM_PMU_STATE_CHANGE_FAILED;
            }
        }
    } while (barman_atomic_cmp_ex_weak_pointer(&bm_settings.per_core_settings[core].init_state, &init_state, BM_PMU_INIT_INITIALIZING));

    /* find the best matched configuration */
    for (index = 0; index < bm_settings.num_pmu_family_settings; ++index) {
        found_match = ((bm_settings.pmu_family_settings[index].midr & BM_MIDR_CPUID_MASK) == (midr & BM_MIDR_CPUID_MASK)) && barman_core_set_is_set(bm_settings.pmu_family_settings[index].allowed_cores, core);
        if (found_match) {
            BM_DEBUG("Found matching PMU settings for processor (midr=0x%x, no=%u): #%u\n", midr, core, index);
            break;
        }
    }

    if (!found_match) {
        BM_ERROR("Unable to initialize PMU for processor (midr=0x%x, no=%u), no matching PMU family settings\n", midr, core);
        barman_atomic_store(&bm_settings.per_core_settings[core].init_state, BM_PMU_INIT_UNINITIALIZED);
        return BM_PMU_STATE_CHANGE_FAILED;
    }

    /* save the pmu family */
    bm_settings.per_core_settings[core].pmu_family = index;

    /* init the pmu */
    num_counters_setup = barman_pmu_init(bm_settings.pmu_family_settings[index].num_events, bm_settings.pmu_family_settings[index].event_types);

    /* validate number of counters */
    if (num_counters_setup < 0) {
        BM_ERROR("Unable to initialize PMU for processor (midr=0x%x, no=%u)\n", midr, core);
        barman_atomic_store(&bm_settings.per_core_settings[core].init_state, BM_PMU_INIT_UNINITIALIZED);
        return BM_PMU_STATE_CHANGE_FAILED;
    }

    /* limit number of counters and store */
    num_counters = BM_MIN((bm_uint32) num_counters_setup, BM_MAX_PMU_COUNTERS);
    bm_settings.per_core_settings[core].num_counters = num_counters;

    /* store the events in the datastore */
#if BM_PMU_HAS_FIXED_CYCLE_COUNTER

    /* insert cycle counter into types map */
    for (counter = 0; counter < num_counters; ++counter) {
        if (BM_PMU_CYCLE_COUNTER_ID == counter) {
            counter_types[counter] = BM_PMU_CYCLE_COUNTER_TYPE;
        }
        else {
            counter_types[counter] = bm_settings.pmu_family_settings[index].event_types[(counter > BM_PMU_CYCLE_COUNTER_ID ? counter - 1 : counter)];
        }
    }

    /* send */
    if (!barman_protocol_write_pmu_settings(barman_ext_get_timestamp(), midr, mpidr, core, num_counters, counter_types)) {
        BM_ERROR("Unable to initialize PMU for processor (midr=0x%x, no=%u), could not store PMU settings\n", midr, core);
        barman_atomic_store(&bm_settings.per_core_settings[core].init_state, BM_PMU_INIT_UNINITIALIZED);
        return BM_PMU_STATE_CHANGE_FAILED;
    }

#else

    /* send */
    if (!barman_protocol_write_pmu_settings(barman_ext_get_timestamp(), midr, mpidr, core, num_counters, bm_settings.pmu_family_settings[index].event_types)) {
        BM_ERROR("Unable to initialize PMU for processor (midr=0x%x, no=%u), could not store PMU settings\n", midr, core);
        barman_atomic_store(&bm_settings.per_core_settings[core].init_state, BM_PMU_INIT_UNINITIALIZED);
        return BM_PMU_STATE_CHANGE_FAILED;
    }

#endif

#if BM_CONFIG_MIN_SAMPLE_PERIOD > 0
    /* initialize the sample sample rate limit variables */
    bm_settings.minimum_sample_period = barman_protocol_get_minimum_sample_period();
    BM_ATOMIC_VAR_INIT(bm_settings.per_core_settings[core].last_sample_timestamp) = barman_ext_get_timestamp();
#endif

    /* mark initialized */
    BM_INFO("Initialize PMU for processor (midr=0x%x, no=%u) with %u counters\n", midr, core, num_counters);
    barman_atomic_store(&bm_settings.per_core_settings[core].init_state, BM_PMU_INIT_INITIALIZED);
    return BM_PMU_STATE_CHANGE_SUCCEEDED;
}

/**
 * @brief   Try to transition the PMU state for a given core to some new state
 * @param   core            The core number
 * @param   current_state   [IN/OUT] The current state value
 * @param   target_state    The new state
 * @return  BM_TRUE if the state was changed, BM_FALSE if the transition is invalid
 * @note    Allowable transitions are:
 *
 *          `BM_PMU_STATE_UNINITIALIZED     -> BM_PMU_STATE_SHOULD_START, BM_PMU_STATE_SHOULD_STOP`
 *          `BM_PMU_STATE_SHOULD_START      -> BM_PMU_STATE_SHOULD_STOP, BM_PMU_STATE_STARTED, BM_PMU_STATE_STOPPED, BM_PMU_STATE_UNINITIALIZED`
 *          `BM_PMU_STATE_SHOULD_STOP       -> BM_PMU_STATE_SHOULD_START, BM_PMU_STATE_STARTED, BM_PMU_STATE_STOPPED, BM_PMU_STATE_UNINITIALIZED`
 *          `BM_PMU_STATE_STARTED           -> BM_PMU_STATE_SHOULD_STOP, BM_PMU_STATE_STOPPED`
 *          `BM_PMU_STATE_STOPPED           -> BM_PMU_STATE_SHOULD_START, BM_PMU_STATE_STARTED`
 */
BM_NONNULL((2))
static enum bm_try_change_pmu_result barman_try_change_pmu_state(bm_uint32 core, enum bm_pmu_target_state * current_state,
                                                                     enum bm_pmu_target_state target_state)
{
    switch (target_state) {
    case BM_PMU_STATE_UNINITIALIZED: {
        switch (*current_state) {
        case BM_PMU_STATE_UNINITIALIZED: {
            /* ok */
            return BM_PMU_STATE_CHANGE_SUCCEEDED;
        }
        case BM_PMU_STATE_SHOULD_START:
        case BM_PMU_STATE_SHOULD_STOP: {
            /* just transition */
            if (barman_atomic_cmp_ex_strong_pointer(&bm_settings.per_core_settings[core].state, current_state, target_state)) {
                return BM_PMU_STATE_CHANGE_SUCCEEDED;
            }
            /* try again */
            break;
        }
        case BM_PMU_STATE_STOPPED:
        case BM_PMU_STATE_STARTED:
        default: {
            /* invalid transition */
            return BM_PMU_STATE_CHANGE_FAILED;
        }
        }
        break;
    }
    case BM_PMU_STATE_SHOULD_START: {
        switch (*current_state) {
        case BM_PMU_STATE_UNINITIALIZED:
        case BM_PMU_STATE_SHOULD_STOP:
        case BM_PMU_STATE_STOPPED: {
            /* just transition */
            if (barman_atomic_cmp_ex_strong_pointer(&bm_settings.per_core_settings[core].state, current_state, target_state)) {
                return BM_PMU_STATE_CHANGE_SUCCEEDED;
            }
            /* try again */
            break;
        }
        case BM_PMU_STATE_SHOULD_START:
        case BM_PMU_STATE_STARTED: {
            /* no change */
            return BM_PMU_STATE_CHANGE_SUCCEEDED;
        }
        default: {
            /* invalid transition */
            return BM_PMU_STATE_CHANGE_FAILED;
        }
        }
        break;
    }
    case BM_PMU_STATE_SHOULD_STOP: {
        switch (*current_state) {
        case BM_PMU_STATE_SHOULD_START:
        case BM_PMU_STATE_STARTED: {
            /* just transition */
            if (barman_atomic_cmp_ex_strong_pointer(&bm_settings.per_core_settings[core].state, current_state, target_state)) {
                return BM_PMU_STATE_CHANGE_SUCCEEDED;
            }
            /* try again */
            break;
        }
        case BM_PMU_STATE_SHOULD_STOP:
        case BM_PMU_STATE_STOPPED: {
            /* no change */
            return BM_PMU_STATE_CHANGE_SUCCEEDED;
        }
        case BM_PMU_STATE_UNINITIALIZED:
        default: {
            /* invalid transition */
            return BM_PMU_STATE_CHANGE_FAILED;
        }
        }
        break;
    }
    case BM_PMU_STATE_STARTED: {
        switch (*current_state) {
        case BM_PMU_STATE_SHOULD_START:
        case BM_PMU_STATE_STOPPED:
        case BM_PMU_STATE_SHOULD_STOP: {
            /* check initialized */
            const enum bm_pmu_init_state init_state = barman_atomic_load(&bm_settings.per_core_settings[core].init_state);

            if (init_state == BM_PMU_INIT_INITIALIZED) {
                /* transition, then start */
                if (barman_atomic_cmp_ex_strong_pointer(&bm_settings.per_core_settings[core].state, current_state, target_state)) {
                    barman_pmu_start();
                    return BM_PMU_STATE_CHANGE_SUCCEEDED;
                }
            }
            else {
                /* try change from should-stop to should-start */
                if (*current_state == BM_PMU_STATE_SHOULD_STOP) {
                    if (barman_atomic_cmp_ex_strong_pointer(&bm_settings.per_core_settings[core].state, current_state, BM_PMU_STATE_SHOULD_START)) {
                        *current_state = BM_PMU_STATE_SHOULD_START;
                    }
                }

                /* invalid transition */
                return BM_PMU_STATE_CHANGE_FAILED;
            }

            /* try again */
            break;
        }
        case BM_PMU_STATE_STARTED: {
            /* no change */
            return BM_PMU_STATE_CHANGE_SUCCEEDED;
        }
        case BM_PMU_STATE_UNINITIALIZED:
        default: {
            /* invalid transition */
            return BM_PMU_STATE_CHANGE_FAILED;
        }
        }
        break;
    }
    case BM_PMU_STATE_STOPPED: {
        switch (*current_state) {
        case BM_PMU_STATE_SHOULD_START:
        case BM_PMU_STATE_SHOULD_STOP:
        case BM_PMU_STATE_STARTED: {
            /* check initialized */
            const enum bm_pmu_init_state init_state = barman_atomic_load(&bm_settings.per_core_settings[core].init_state);

            if (init_state == BM_PMU_INIT_INITIALIZED) {
                /* transition, then stop */
                if (barman_atomic_cmp_ex_strong_pointer(&bm_settings.per_core_settings[core].state, current_state, target_state)) {
                    barman_pmu_stop();
                    return BM_PMU_STATE_CHANGE_SUCCEEDED;
                }
            }
            else {
                /* try change from should-start to should-stop */
                if (*current_state == BM_PMU_STATE_SHOULD_START) {
                    if (barman_atomic_cmp_ex_strong_pointer(&bm_settings.per_core_settings[core].state, current_state, BM_PMU_STATE_SHOULD_STOP)) {
                        *current_state = BM_PMU_STATE_SHOULD_STOP;
                    }
                }

                /* invalid transition */
                return BM_PMU_STATE_CHANGE_FAILED;
            }

            /* try again */
            break;
        }
        case BM_PMU_STATE_STOPPED: {
            /* no change */
            return BM_PMU_STATE_CHANGE_SUCCEEDED;
        }

        case BM_PMU_STATE_UNINITIALIZED:
        default: {
            /* invalid transition */
            return BM_PMU_STATE_CHANGE_FAILED;
        }
        }
        break;
    }
    default: {
        /* unknown transition */
        return BM_PMU_STATE_CHANGE_FAILED;
    }
    }

    return BM_PMU_STATE_CHANGE_RETRY;
}

/**
 * @brief   Transition the PMU state for a given core to some new state
 * @param   core            The core number
 * @param   target_state    The new state
 * @return  BM_TRUE if the state was changed, BM_FALSE if the transition is invalid
 */
static BM_INLINE bm_bool barman_change_pmu_state(bm_uint32 core, enum bm_pmu_target_state target_state)
{
    bm_atomic_pmu_target_state current_state = barman_atomic_load(&bm_settings.per_core_settings[core].state);
    enum bm_try_change_pmu_result result;

    do {
        result = barman_try_change_pmu_state(core, &current_state, target_state);
    } while (result == BM_PMU_STATE_CHANGE_RETRY);

    return result == BM_PMU_STATE_CHANGE_SUCCEEDED;
}

/**
 * @brief   Attempt to init the PMU and then transition to the correct state
 * @param   core            The core number
 * @param   current_state   [IN/OUT] The current state
 * @param   target_state    The target state
 * @retval  BM_PMU_STATE_CHANGE_SUCCEEDED   If the PMU was initialized and the state changed
 * @retval  BM_PMU_STATE_CHANGE_FAILED      If the PMU is busy being initialized by another thread
 * @retval  BM_PMU_STATE_CHANGE_RETRY       If the state change failed
 */
static BM_INLINE enum bm_try_change_pmu_result barman_init_and_transition_pmu(bm_uint32 core,enum bm_pmu_target_state * current_state, enum bm_pmu_target_state target_state)
{
    /* init if necessary */
    enum bm_try_change_pmu_result result = barman_initialize_pmu(core);

    if (result == BM_PMU_STATE_CHANGE_SUCCEEDED) {
        /* The init succeeded; try to change state */
        return barman_try_change_pmu_state(core, current_state, target_state);
    }

    else if (result == BM_PMU_STATE_CHANGE_FAILED) {
        /* state must become uninitialized */
        barman_atomic_store(&bm_settings.per_core_settings[core].state, BM_PMU_STATE_UNINITIALIZED);
        *current_state = BM_PMU_STATE_UNINITIALIZED;
    }

    /* either the PMU failed to initialized, or the PMU is being initialized by another thread so fail to prevent deadlock */
    return BM_PMU_STATE_CHANGE_FAILED;
}

/**
 * @brief   Transitions the state from a `SHOULD_xxx` state at the start of a sample
 * @param   core    The current core number
 * @return  The state after any transition
 */
static BM_INLINE enum bm_pmu_target_state barman_transition_pmu_state_on_sample(bm_uint32 core)
{
    enum bm_pmu_target_state current_state = barman_atomic_load(&bm_settings.per_core_settings[core].state);
    enum bm_try_change_pmu_result result;

    do
    {
        switch (current_state)
        {
            case BM_PMU_STATE_SHOULD_START:
            case BM_PMU_STATE_SHOULD_STOP: {
                const enum bm_pmu_target_state target_state = (current_state == BM_PMU_STATE_SHOULD_START ? BM_PMU_STATE_STARTED : BM_PMU_STATE_STOPPED);

                /* init and transition */
                result = barman_init_and_transition_pmu(core, &current_state, target_state);

                if (result == BM_PMU_STATE_CHANGE_SUCCEEDED) {
                    /* the pmu was initialized and state change succeeded */
                    return target_state;
                }
                else if (result == BM_PMU_STATE_CHANGE_FAILED) {
                    /* the pmu was in the process of initializing, fail as another thread is busy */
                    return current_state;
                }

                /* try again */
                break;
            }
            case BM_PMU_STATE_STOPPED:
            case BM_PMU_STATE_STARTED:
            case BM_PMU_STATE_UNINITIALIZED:
            default: {
                /* no change */
                return current_state;
            }
        }
    }
    while (1);
}

#if BM_NUM_CUSTOM_COUNTERS > 0

/**
 * @brief   Read a single custom counter value
 * @param   counter     The index of the counter to read
 * @param   value_out   The pointer to write the counter value to
 * @return  BM_TRUE if the value was sampled, BM_FALSE if not (i.e. is not a sampled counter, or failed to read value)
 */
BM_NONNULL((2))
static bm_bool barman_sample_custom_counter(bm_uint32 counter, bm_uint64 * value_out)
{
    if ((counter < BM_NUM_CUSTOM_COUNTERS) && (BM_CUSTOM_CHARTS_SERIES[counter]->sampling_function != BM_NULL)) {
        return BM_CUSTOM_CHARTS_SERIES[counter]->sampling_function(value_out);
    }

    return BM_FALSE;
}

#endif

/** @} */

/* ********************************** */

bm_bool barman_initialize_pmu_family(bm_uint32 midr, bm_uint32 n_event_types, const bm_uint32 * event_types,
                                     const bm_core_set allowed_cores)
{
    const bm_size_t allowed_length = sizeof(bm_core_set);
    bm_uint32 p_index, d_index;

    /* check not already started */
    if (barman_atomic_load(&bm_settings.started)) {
        BM_ERROR("Cannot configure a new PMU family once sampling is started\n");
        return BM_FALSE;
    }

    /* check not full */
    if (bm_settings.num_pmu_family_settings >= BM_COUNT_OF(bm_settings.pmu_family_settings)) {
        BM_ERROR("No more space for configuration settings when configuring PMU family\n");
        return BM_FALSE;
    }

    /* iterate over PMU configurations to check not already set */
    for (p_index = 0; p_index < bm_settings.num_pmu_family_settings; ++p_index) {
        /* check not same MIDR */
        if (bm_settings.pmu_family_settings[p_index].midr == midr) {
            /* check no overlap of allowed cores */
            for (d_index = 0; d_index < allowed_length; ++d_index) {
                const bm_uint8 allowed_pmu = (bm_settings.pmu_family_settings[p_index].allowed_cores[d_index]);
                const bm_uint8 allowed_new = (allowed_cores != BM_NULL ? allowed_cores[d_index] : ~0);
                /* must be no overlap of allowed cores */
                if ((allowed_pmu & allowed_new) != 0) {
                    BM_ERROR("Overlapping core bitmaps when configuring new PMU family\n");
                    return BM_FALSE;
                }
            }
        }
    }

    /* write the new configuration */
    p_index = bm_settings.num_pmu_family_settings++;
    bm_settings.pmu_family_settings[p_index].midr = midr;
    bm_settings.pmu_family_settings[p_index].num_events = BM_MIN(n_event_types, BM_COUNT_OF(bm_settings.pmu_family_settings[p_index].event_types));
    for (d_index = 0; d_index < bm_settings.pmu_family_settings[p_index].num_events; ++d_index) {
        bm_settings.pmu_family_settings[p_index].event_types[d_index] = event_types[d_index];
    }
    for (d_index = 0; d_index < allowed_length; ++d_index) {
        bm_settings.pmu_family_settings[p_index].allowed_cores[d_index] = (allowed_cores != BM_NULL ? allowed_cores[d_index] : ~0);
    }

    BM_INFO("PMU family #%u configured as (midr=0x%x, n_event_types=%u)\n", p_index, midr, bm_settings.pmu_family_settings[p_index].num_events);

    return BM_TRUE;
}

void barman_enable_sampling(void)
{
    bm_uint32 index;

    barman_atomic_store(&bm_settings.started, BM_TRUE);

    for (index = 0; index < BM_CONFIG_MAX_CORES; ++index) {
        barman_change_pmu_state(index, BM_PMU_STATE_SHOULD_START);
    }

#if BM_CONFIG_MAX_CORES == 1
    /*
     * If we only have one core (that's this one) so we can start
     * the PMU now without waiting for barman_sample_counters to be called
     * on the core.
     */
    barman_transition_pmu_state_on_sample(0);
#endif
}

void barman_disable_sampling(void)
{
    bm_uint32 index;

    for (index = 0; index < BM_CONFIG_MAX_CORES; ++index) {
        barman_change_pmu_state(index, BM_PMU_STATE_SHOULD_STOP);
    }

#if BM_CONFIG_MAX_CORES == 1
    barman_transition_pmu_state_on_sample(0);
#endif
}

void barman_sample_counters(bm_bool sample_return_address)
{
    barman_sample_counters_with_program_counter(sample_return_address ? BM_GET_RETURN_ADDRESS() : BM_NULL);
}

void barman_sample_counters_with_program_counter(const void * pc)
{
    const bm_uint32 core = barman_get_core_no();

    enum bm_pmu_target_state current_state;
    bm_uint64 counter_values[BM_MAX_PMU_COUNTERS] = { BM_PMU_INVALID_COUNTER_VALUE };
    bm_uint64 timestamp;
    bm_uint32 num_counters;
    bm_uint32 counter;
#if BM_CONFIG_MIN_SAMPLE_PERIOD > 0
    bm_bool was_successful;
    bm_uint64 last_timestamp;
    bm_uint64 min_period;
#endif
    bm_uint32 valid_custom_counters = 0;
    bm_uint32 custom_counter_ids[BM_CUSTOM_COUNTER_ARRAY_SIZE];
    bm_uint64 custom_counter_values[BM_CUSTOM_COUNTER_ARRAY_SIZE];

    /* validate core */
    if (core >= BM_CONFIG_MAX_CORES) {
        return;
    }

    /* validate initialized and started; transition state if required */
    current_state = barman_transition_pmu_state_on_sample(core);
    if (current_state != BM_PMU_STATE_STARTED) {
        return;
    }

    /* validate has some counters */
    num_counters = bm_settings.per_core_settings[core].num_counters;
    if (num_counters == 0) {
        return;
    }

    /* get timestamp */
    timestamp = barman_ext_get_timestamp();

#if BM_CONFIG_MIN_SAMPLE_PERIOD > 0
    last_timestamp = barman_atomic_load(&bm_settings.per_core_settings[core].last_sample_timestamp);
    min_period = bm_settings.minimum_sample_period;

    if ((last_timestamp + min_period) > timestamp) {
        /* Skip this sample */
        return;
    }
#endif

    /* read all the counters */
    for (counter = 0; counter < num_counters; ++counter) {
        counter_values[counter] = barman_pmu_read_counter(counter);
    }

    /* sample custom counters */
#if BM_NUM_CUSTOM_COUNTERS > 0
    for (counter = 0; counter < BM_NUM_CUSTOM_COUNTERS; ++counter) {
        if (barman_sample_custom_counter(counter, &(custom_counter_values[valid_custom_counters]))) {
            custom_counter_ids[valid_custom_counters] = counter;
            valid_custom_counters += 1;
        }
    }
#endif

    /* write the sample */

#if BM_CONFIG_MIN_SAMPLE_PERIOD > 0
    was_successful =
#endif

    barman_protocol_write_sample(timestamp, core,
#if BM_CONFIG_MAX_TASK_INFOS > 0
                                 barman_ext_get_current_task_id(),
#endif
                                 pc, num_counters, counter_values,
                                 valid_custom_counters, custom_counter_ids, custom_counter_values);

#if BM_CONFIG_MIN_SAMPLE_PERIOD > 0
    if (was_successful) {
        barman_atomic_store(&bm_settings.per_core_settings[core].last_sample_timestamp, timestamp);
    }
#endif
}

#if BM_CONFIG_MAX_TASK_INFOS > 0
void barman_record_task_switch(enum bm_task_switch_reason reason)
{
    const bm_uint32 core = barman_get_core_no();

    bm_uint64 timestamp;

    /* validate core */
    if (core >= BM_CONFIG_MAX_CORES) {
        return;
    }

    /* get timestamp */
    timestamp = barman_ext_get_timestamp();

    /* write the task switch record */
    barman_protocol_write_task_switch(timestamp, core, barman_ext_get_current_task_id(), reason);
}
#endif

void barman_wfi(void)
{
    const bm_uint32 core = barman_get_core_no();

    /* validate core */
    if (core >= BM_CONFIG_MAX_CORES) {
        return;
    }

    /* send before event */
    barman_protocol_write_halt_event(barman_ext_get_timestamp(), core, BM_TRUE);

    /* do WFI */
    barman_wfi_intrinsic();

    /* send after event */
    barman_protocol_write_halt_event(barman_ext_get_timestamp(), core, BM_FALSE);
}

void barman_wfe(void)
{
    const bm_uint32 core = barman_get_core_no();

    /* validate core */
    if (core >= BM_CONFIG_MAX_CORES) {
        return;
    }

    /* send before event */
    barman_protocol_write_halt_event(barman_ext_get_timestamp(), core, BM_TRUE);

    /* do WFE */
    barman_wfe_intrinsic();

    /* send after event */
    barman_protocol_write_halt_event(barman_ext_get_timestamp(), core, BM_FALSE);
}

void barman_before_idle(void)
{
    const bm_uint32 core = barman_get_core_no();

    /* validate core */
    if (core >= BM_CONFIG_MAX_CORES) {
        return;
    }

    /* send before event */
    barman_protocol_write_halt_event(barman_ext_get_timestamp(), core, BM_TRUE);
}

void barman_after_idle(void)
{
    const bm_uint32 core = barman_get_core_no();

    /* validate core */
    if (core >= BM_CONFIG_MAX_CORES) {
        return;
    }

    /* send before event */
    barman_protocol_write_halt_event(barman_ext_get_timestamp(), core, BM_FALSE);
}

/**
 * @brief   Annotation types
 */
enum bm_annotation_types
{
    BM_ANNOTATION_TYPE_STRING = 0,       /**< A text annotation */
    BM_ANNOTATION_TYPE_BOOKMARK = 1,     /**< A book mark annotation */
    BM_ANNOTATION_TYPE_CHANNEL_NAME = 2, /**< An instruction to name a channel */
    BM_ANNOTATION_TYPE_GROUP_NAME = 3    /**< An instruction to name a group */
};

static void barman_annotate_generic_string(enum bm_annotation_types type, bm_uint32 channel, bm_uint32 group, bm_uint32 color, const char * string)
{
    const bm_uint32 core = barman_get_core_no();
    bm_uintptr string_length = 0;

    /* validate core */
    if (core >= BM_CONFIG_MAX_CORES) {
        return;
    }

    if (string != BM_NULL)
    {
        while (string[string_length] != 0) string_length++;
        string_length++; /* and the null byte */
    }

    barman_protocol_write_annotation(barman_ext_get_timestamp(), core,
#if BM_CONFIG_MAX_TASK_INFOS > 0
                                     barman_ext_get_current_task_id(),
#endif
                                     type, channel, group, color, string_length, (const bm_uint8 *) string);
}

void barman_annotate_channel(bm_uint32 channel, bm_uint32 color, const char * text)
{
    barman_annotate_generic_string(BM_ANNOTATION_TYPE_STRING, channel, 0, color, text);
}

void barman_annotate_name_channel(bm_uint32 channel, bm_uint32 group, const char * name)
{
    barman_annotate_generic_string(BM_ANNOTATION_TYPE_CHANNEL_NAME, channel, group, 0, name);
}

void barman_annotate_name_group(bm_uint32 group, const char * name)
{
    barman_annotate_generic_string(BM_ANNOTATION_TYPE_GROUP_NAME, 0, group, 0, name);
}

void barman_annotate_marker(bm_uint32 color, const char * text)
{
    barman_annotate_generic_string(BM_ANNOTATION_TYPE_BOOKMARK, 0, 0, color, text);
}
