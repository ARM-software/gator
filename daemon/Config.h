/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#ifndef CONFIG_H
#define CONFIG_H

#define STRIFY2(ARG) #ARG
#define STRIFY(ARG) STRIFY2(ARG)

#define ARRAY_LENGTH(A) static_cast<int>(sizeof(A) / sizeof((A)[0]))

#define MAX_PERFORMANCE_COUNTERS 100

// feature control options
#ifndef CONFIG_PREFER_SYSTEM_WIDE_MODE
#define CONFIG_PREFER_SYSTEM_WIDE_MODE 1
#endif

#ifndef CONFIG_SUPPORT_PROC_POLLING
#define CONFIG_SUPPORT_PROC_POLLING 1
#endif

#ifndef CONFIG_SUPPORT_PERF
#define CONFIG_SUPPORT_PERF 1
#endif

#ifndef GATORD_BUILD_ID
#define GATORD_BUILD_ID "oss"
#endif

#ifndef GATOR_SELF_PROFILE
#define GATOR_SELF_PROFILE 0
#endif

// assume /proc/sys/kernel/perf_event_paranoid == 2 if it cannot be read
#ifndef CONFIG_ASSUME_PERF_HIGH_PARANOIA
#define CONFIG_ASSUME_PERF_HIGH_PARANOIA 1
#endif

#if !CONFIG_SUPPORT_PERF && !CONFIG_SUPPORT_PROC_POLLING
#error "at least one of CONFIG_SUPPORT_PERF and CONFIG_SUPPORT_PROC_POLLING must be set"
#endif

#endif // CONFIG_H
