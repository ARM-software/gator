/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_DATA_STORE_TYPES
#define INCLUDE_BARMAN_DATA_STORE_TYPES

#include "barman-types.h"
#include "barman-config.h"
#include "data-store/barman-stm.h"
#include "data-store/barman-itm.h"

#if BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_LINEAR_RAM_BUFFER
#   define BM_DATASTORE_IS_IN_MEMORY                            1
#   define BM_DATASTORE_USES_STREAMING_INTERFACE                0

#elif BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_CIRCULAR_RAM_BUFFER
#   define BM_DATASTORE_IS_IN_MEMORY                            1
#   define BM_DATASTORE_USES_STREAMING_INTERFACE                0

#elif BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_STM
#   define BM_DATASTORE_IS_IN_MEMORY                            0
#   define BM_DATASTORE_USES_STREAMING_INTERFACE                1
    typedef struct bm_datastore_config_stm bm_datastore_config;

#elif BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_ITM
#   define BM_DATASTORE_IS_IN_MEMORY                            0
#   define BM_DATASTORE_USES_STREAMING_INTERFACE                1
    typedef struct bm_datastore_config_itm bm_datastore_config;

#elif BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_STREAMING_USER_SUPPLIED
#   define BM_DATASTORE_IS_IN_MEMORY                            0
#   define BM_DATASTORE_USES_STREAMING_INTERFACE                1
    typedef void * bm_datastore_config;

#elif BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_USER_SUPPLIED
# ifndef BM_CONFIG_DATASTORE_USER_SUPPLIED_IS_IN_MEMORY
#   error "BM_CONFIG_DATASTORE_USER_SUPPLIED_IS_IN_MEMORY must be defined true or false"
# endif
#   define BM_DATASTORE_IS_IN_MEMORY                            BM_CONFIG_DATASTORE_USER_SUPPLIED_IS_IN_MEMORY
#   define BM_DATASTORE_USES_STREAMING_INTERFACE                0

#if !BM_DATASTORE_IS_IN_MEMORY
    typedef void * bm_datastore_config;
#endif

#else
#   error "BM_CONFIG_USE_DATASTORE is not set correctly"
#endif


#if BM_DATASTORE_IS_IN_MEMORY
    /**
     * Datastore configuration for in memory datastores
     */
    struct bm_datastore_config_in_memory
    {
        /**  Pointer to in memory buffer */
        bm_uint8 * buffer;
        /** The length of the in memory buffer, or 0 if configured for other type of data store */
        bm_uintptr buffer_length;
    };

    typedef struct bm_datastore_config_in_memory bm_datastore_config;
#endif

#endif /* INCLUDE_BARMAN_DATA_STORE_TYPES */
