/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_API
#define INCLUDE_BARMAN_API

#include "barman-api-public.h"
#include "barman-core-set.h"

/**
 * @brief   Set the PMU configuration settings for the PMU family that matches a given MIDR
 * @param   midr                The MIDR that the data is for
 * @param   n_event_types       The number of configurable events to enable
 * @param   event_types         An array of length `n_event_types` containing the event types for the events to enable
 * @param   allowed_cores       Array enumerating the cores that should be included in this configuration. BM_NULL will be interpreted as all *included*.
 * @return  BM_TRUE on success, BM_FALSE on failure.
 * @note    This call will fail if the data for a MIDR (and cores) has already been programmed
 * @ingroup bm_public
 */
BM_NONNULL((3))
bm_bool barman_initialize_pmu_family(bm_uint32 midr, bm_uint32 n_event_types, const bm_uint32 * event_types, const bm_core_set allowed_cores);

#endif /* INCLUDE_BARMAN_API */
