/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_CUSTOM_COUNTERS
#define INCLUDE_BARMAN_CUSTOM_COUNTERS

#include "barman-types-public.h"

/**
 * @defgroup    bm_custom_counters  Custom counter chart related items
 * @{
 */

/**
 * @brief   Chart series composition
 */
enum bm_custom_counter_chart_series_composition
{
    BM_SERIES_COMPOSITION_STACKED = 1, /**< Stacked series */
    BM_SERIES_COMPOSITION_OVERLAY = 2, /**< Overlay series*/
    BM_SERIES_COMPOSITION_LOG10 = 3 /**< Log10 series */
};

/**
 * @brief   Chart rendering type
 */
enum bm_custom_counter_chart_rendering_type
{
    BM_RENDERING_TYPE_FILLED = 1, /**< Filled chart */
    BM_RENDERING_TYPE_LINE = 2, /**< Line chart */
    BM_RENDERING_TYPE_BAR = 3 /**< Bar chart */
};

/**
 * @brief   The series data class
 */
enum bm_custom_counter_series_class
{
    BM_SERIES_CLASS_DELTA = 1, /**< Delta value */
    BM_SERIES_CLASS_INCIDENT = 2, /**< Incidental delta value */
    BM_SERIES_CLASS_ABSOLUTE = 3 /**< Absolute value */
};

/**
 * @brief   The series display type
 */
enum bm_custom_counter_series_display
{
    BM_SERIES_DISPLAY_ACCUMULATE = 1, /**< Accumulate delta values */
    BM_SERIES_DISPLAY_AVERAGE = 2, /**< Average absolute values */
    BM_SERIES_DISPLAY_HERTZ = 3, /**< Accumulate and average over one second */
    BM_SERIES_DISPLAY_MAXIMUM = 4, /**< Maximum absolute value */
    BM_SERIES_DISPLAY_MINIMUM = 5 /**< Minimum absolute value */
};

/**
 * @brief   Custom counter sampling function type
 * @param   value_out   A non-null pointer to a uint64 that will contain the sample value on successful read.
 * @return  BM_TRUE for successful read of sample, BM_FALSE otherwise
 */
typedef bm_bool (* bm_custom_counter_sampling_function)(bm_uint64 * value_out);

/**
 * @brief   Description of a custom chart series
 */
struct bm_custom_counter_chart_series
{
    /** The index of the chart the series belongs to */
    bm_uint32 chart_index;
    /** The name of the series */
    const char * name;
    /** Series units */
    const char * units;
    /** Description */
    const char * description;
    /** Data class */
    enum bm_custom_counter_series_class clazz;
    /** Display type */
    enum bm_custom_counter_series_display display;
    /** Multiplier value */
    double multiplier;
    /** Series colour */
    bm_uint32 colour;
    /** Sampling function; is NULL for push counters */
    bm_custom_counter_sampling_function sampling_function;
};

/**
 * @brief   Description of custom chart
 */
struct bm_custom_counter_chart
{
    /** The name of the chart */
    const char * name;
    /** The series composition */
    enum bm_custom_counter_chart_series_composition series_composition;
    /** The rendering type */
    enum bm_custom_counter_chart_rendering_type rendering_type;
    /** Average CSM selection */
    bm_bool average_selection;
    /** Average cores in aggregate view */
    bm_bool average_cores;
    /** Take percentage of max value */
    bm_bool percentage;
    /** Series are per-cpu */
    bm_bool per_cpu;
    /** The number of series */
    bm_uint32 num_series;
    /** The series */
    const struct bm_custom_counter_chart_series * const * series;
};

/** @} */

#endif /* INCLUDE_BARMAN_CUSTOM_COUNTERS */
