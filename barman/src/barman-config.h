/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_CONFIG
#define INCLUDE_BARMAN_CONFIG

/**
 * @defgroup    bm_config   Config settings
 * @{ */

/**
 * @def     BM_CONFIG_MAX_CORES
 * @brief   The maximum number of processor elements supported
 */
#ifndef BM_CONFIG_MAX_CORES
#define BM_CONFIG_MAX_CORES                 8
#endif

/**
 * @def     BM_CONFIG_NUM_PMU_TYPES
 * @brief   The number of processor types supported
 */
#ifndef BM_CONFIG_NUM_PMU_TYPES
#define BM_CONFIG_NUM_PMU_TYPES             BM_CONFIG_MAX_CORES
#endif

/**
 * @def     BM_CONFIG_ENABLE_BUILTIN_MEMFUNCS
 * @brief   When set true, will enable the use of __builtin_memset, __builtin_memcpy etc.
 */
#ifndef BM_CONFIG_ENABLE_BUILTIN_MEMFUNCS
#define BM_CONFIG_ENABLE_BUILTIN_MEMFUNCS   0
#endif

/**
 * @def     BM_CONFIG_USER_SUPPLIED_PMU_DRIVER
 * @brief   The PMU API parameters are defined externally to barman-api.c
 * @details When enabled the user must provide the following defined functions and macros
 *
 *          `#define BM_MAX_PMU_COUNTERS`
 *          - Defines the maximum number of PMU counters the hardware supports
 *          `#define BM_PMU_INVALID_COUNTER_VALUE`
 *          - Defines the value the PMU driver will return if the counter was not read
 *          `#define BM_PMU_HAS_FIXED_CYCLE_COUNTER`
 *          - Must be defined as a boolean value, indicating whether or not the PMU has a fixed cycle counter separate
 *            from the other counters that is always enabled
 *          `#define BM_PMU_CYCLE_COUNTER_ID`
 *          - If BM_PMU_HAS_FIXED_CYCLE_COUNTER is defined true, this must be defined with the counter number for the
 *            fixed cycle counter
 *          `#define BM_PMU_CYCLE_COUNTER_TYPE`
 *          - The event type as defined in the `events.xml` for the fixed cycle counter
 *          `void barman_ext_pmu_init(bm_uint32 n_event_types, const bm_uint32 * event_types)`
 *          - The PMU initialization function for the current core
 *          `void barman_ext_pmu_start(void)`
 *          - The PMU start counting function for the current core
 *          `void barman_ext_pmu_stop(void)`
 *          - The PMU stop counting function for the current core
 *          `bm_uint64 barman_pmu_read_counter(bm_uint32 counter)`
 *          - The PMU counter read function
 */
#ifndef BM_CONFIG_USER_SUPPLIED_PMU_DRIVER
#define BM_CONFIG_USER_SUPPLIED_PMU_DRIVER  0
#endif

/** Value to define {@link BM_CONFIG_USE_DATASTORE} as if the user is to supply the datastore functions */
#define BM_CONFIG_USE_DATASTORE_USER_SUPPLIED             0
/** Value to define {@link BM_CONFIG_USE_DATASTORE} as if the linear ram buffer is used as the data store */
#define BM_CONFIG_USE_DATASTORE_LINEAR_RAM_BUFFER         1
/** Value to define {@link BM_CONFIG_USE_DATASTORE} as if the circular ram buffer is used as the data store */
#define BM_CONFIG_USE_DATASTORE_CIRCULAR_RAM_BUFFER       2
/** Value to define {@link BM_CONFIG_USE_DATASTORE} as if the STM interface is used as the data store */
#define BM_CONFIG_USE_DATASTORE_STM                       3
/** Value to define {@link BM_CONFIG_USE_DATASTORE} as if the user is to supply streaming interface functions */
#define BM_CONFIG_USE_DATASTORE_STREAMING_USER_SUPPLIED   4
/** Value to define {@link BM_CONFIG_USE_DATASTORE} as if the ITM interface is used as the data store */
#define BM_CONFIG_USE_DATASTORE_ITM 5

/**
 * @def     BM_CONFIG_USE_DATASTORE
 * @brief   Specifies the data store to use
 */
#ifndef BM_CONFIG_USE_DATASTORE
#define BM_CONFIG_USE_DATASTORE                     BM_CONFIG_USE_DATASTORE_LINEAR_RAM_BUFFER
#endif

/**
 * @def     BM_CONFIG_ENABLE_LOGGING
 * @brief   When set true will enable logging messages
 */
#ifndef BM_CONFIG_ENABLE_LOGGING
#define BM_CONFIG_ENABLE_LOGGING                    0
#endif

/**
 * @def     BM_CONFIG_ENABLE_DEBUG_LOGGING
 * @brief   When set true (and BM_CONFIG_ENABLE_LOGGING is set true) will enable debug logging messages
 */
#ifndef BM_CONFIG_ENABLE_DEBUG_LOGGING
#define BM_CONFIG_ENABLE_DEBUG_LOGGING              (BM_CONFIG_ENABLE_LOGGING && !defined(NDEBUG))
#endif

/**
 * @def     BM_CONFIG_MAX_MMAP_LAYOUTS
 * @brief   The maximum number of mmap layout entries that will be stored in the data header. Should be configured
 *          to reflect the number of sections for any process images that should be mapped.
 */
#ifndef BM_CONFIG_MAX_MMAP_LAYOUTS
#define BM_CONFIG_MAX_MMAP_LAYOUTS                  1
#endif

/**
 * @def     BM_CONFIG_MAX_TASK_INFOS
 * @brief   Include information about current task/process/thread when sampling when non zero
 * @details For single threaded applications this may be defined zero to indicate that no task information may be saved.
 *          For multi threaded applications / RTOS this value indicates the maximum number of entries to store in the data header
 *          for describing process/threads/tasks.
 */
#ifndef BM_CONFIG_MAX_TASK_INFOS
#define BM_CONFIG_MAX_TASK_INFOS                    1
#endif

/**
 * @def     BM_CONFIG_MIN_SAMPLE_PERIOD
 * @brief   The minimum period between samples in ns. Any samples more frequent will be ignored.
 * @details This is performed on a per core basis.
 */
#ifndef BM_CONFIG_MIN_SAMPLE_PERIOD
#define BM_CONFIG_MIN_SAMPLE_PERIOD                 0
#endif

/**
 * @def     BM_CONFIG_RECORDS_PER_HEADER_SENT
 * @brief   How often a header should be sent.
 * @details The number of sample records sent between sending the header. Ignored for in memory datastores.
 */
#ifndef BM_CONFIG_RECORDS_PER_HEADER_SENT
#define BM_CONFIG_RECORDS_PER_HEADER_SENT           500
#endif

/**
 * @def     BM_CONFIG_STM_MIN_CHANNEL_NUMBER
 * @brief   The minimum channel number the STM datastore will use
 * @details The STM datastore will use {@link BM_CONFIG_STM_NUMBER_OF_CHANNELS} starting from this one.
 *          NB: If this is overridden it must be updated in `barman.xml` for the import process.
 */
#ifndef BM_CONFIG_STM_MIN_CHANNEL_NUMBER
#define BM_CONFIG_STM_MIN_CHANNEL_NUMBER            0
#endif

/**
 * @def     BM_CONFIG_STM_NUMBER_OF_CHANNELS
 * @brief   The number of channels the STM datastore will use
 * @details To ensure no data loss this should be at least the number of possible simultaneous calls of the
 *          barman API functions.
 *          NB: If this is overridden it must be updated in `barman.xml` for the import process.
 */
#ifndef BM_CONFIG_STM_NUMBER_OF_CHANNELS
#define BM_CONFIG_STM_NUMBER_OF_CHANNELS            BM_CONFIG_MAX_CORES
#endif

/**
 * @def     BM_CONFIG_ITM_MIN_PORT_NUMBER
 * @brief   The minimum port number the ITM datastore will use
 * @details The ITM datastore will use {@link BM_CONFIG_ITM_NUMBER_OF_PORTS} starting from this one.
 *          NB: If this is overridden it must be updated in `barman.xml` for the import process.
 */
#ifndef BM_CONFIG_ITM_MIN_PORT_NUMBER
#define BM_CONFIG_ITM_MIN_PORT_NUMBER               0
#endif

/**
 * @def     BM_CONFIG_ITM_NUMBER_OF_PORTS
 * @brief   The number of ports the ITM datastore will use
 * @details To ensure no data loss this should be at least the number of possible simultaneous calls of the
 *          barman API functions.
 *          NB: If this is overridden it must be updated in `barman.xml` for the import process.
 */
#ifndef BM_CONFIG_ITM_NUMBER_OF_PORTS
#define BM_CONFIG_ITM_NUMBER_OF_PORTS               BM_CONFIG_MAX_CORES
#endif

/**
 * @def     BM_CONFIG_DWT_SAMPLE_PERIOD
 * @brief   Number of cycles per PC sample or cycle overflow event.
 * @details Valid values are 64 * i or 1024 * i where i is between
 *          1 and 16 inclusive. Other values will be rounded down.
 *          NB: If this is overridden it must be updated in `barman.xml` for the import process.
 */
#ifndef BM_CONFIG_DWT_SAMPLE_PERIOD
#define BM_CONFIG_DWT_SAMPLE_PERIOD                 1024
#endif

/**
 * @def     BARMAN_DISABLED
 * @brief   When defined to a non-zero value will disable the barman entry points at compile time.
 * @details Use to conditionally disable calls to barman (e.g. in production code).
 */
#ifndef BARMAN_DISABLED
#define BARMAN_DISABLED                             0
#endif

/** @} */

/** @{ */
#if (!defined(BARMAN_DISABLED)) || (!BARMAN_DISABLED)
#   define  BM_PUBLIC_FUNCTION                              extern BM_NEVER_INLINE
#   define  BM_PUBLIC_FUNCTION_BODY_STATEMENT(statement)    ;
#   define  BM_PUBLIC_FUNCTION_BODY(val)                    ;
#   define  BM_PUBLIC_FUNCTION_BODY_VOID                    ;
#else
#   define  BM_PUBLIC_FUNCTION                              static BM_ALWAYS_INLINE
#   define  BM_PUBLIC_FUNCTION_BODY_STATEMENT(statement)    { statement; }
#   define  BM_PUBLIC_FUNCTION_BODY(val)                    { return (val); }
#   define  BM_PUBLIC_FUNCTION_BODY_VOID                    {}
#endif

/** @} */

#endif /* INCLUDE_BARMAN_CONFIG */
