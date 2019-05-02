/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CONFIG_H
#define CONFIG_H

#define STRIFY2(ARG) #ARG
#define STRIFY(ARG) STRIFY2(ARG)

#define ARRAY_LENGTH(A) static_cast<int>(sizeof(A) / sizeof((A)[0]))

#define MAX_PERFORMANCE_COUNTERS 100

// If debugfs is not mounted at /sys/kernel/debug, update TRACING_PATH
#define TRACING_PATH "/sys/kernel/debug/tracing"
#define EVENTS_PATH TRACING_PATH "/events"

// feature control options
#ifndef CONFIG_PREFER_SYSTEM_WIDE_MODE
#define CONFIG_PREFER_SYSTEM_WIDE_MODE  1
#endif

#ifndef CONFIG_SUPPORT_GATOR_KO
#define CONFIG_SUPPORT_GATOR_KO         1
#endif

#ifndef CONFIG_SUPPORT_PROC_POLLING
#define CONFIG_SUPPORT_PROC_POLLING     1
#endif

#ifndef CONFIG_SUPPORT_PERF
#define CONFIG_SUPPORT_PERF             1
#endif

#ifndef GATORD_BUILD_ID
#define GATORD_BUILD_ID                 "oss"
#endif

#if !CONFIG_SUPPORT_PERF && !CONFIG_SUPPORT_GATOR_KO && !CONFIG_SUPPORT_PROC_POLLING
#   error   "at least one of CONFIG_SUPPORT_GATOR_KO, CONFIG_SUPPORT_PERF and CONFIG_SUPPORT_PROC_POLLING must be set"
#endif

#endif // CONFIG_H
