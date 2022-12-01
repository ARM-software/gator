/* Copyright (C) 2010-2022 by Arm Limited. All rights reserved. */

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

#ifndef CONFIG_PERF_SUPPORT_REGISTER_UNWINDING
#if defined(__arm__) || defined(__aarch64__)
#define CONFIG_PERF_SUPPORT_REGISTER_UNWINDING 1
#else
#define CONFIG_PERF_SUPPORT_REGISTER_UNWINDING 0
#endif
#endif

#ifndef CONFIG_DISABLE_CONTINUATION_TRACING
#define CONFIG_DISABLE_CONTINUATION_TRACING 0
#endif

#ifndef CONFIG_ASSERTIONS
#if (!defined(NDEBUG) || (defined(GATOR_UNIT_TESTS) && GATOR_UNIT_TESTS))
#define CONFIG_ASSERTIONS 1
#else
#define CONFIG_ASSERTIONS 0
#endif
#endif

#ifndef CONFIG_LOG_TRACE
#if (!defined(NDEBUG) || (defined(GATOR_UNIT_TESTS) && GATOR_UNIT_TESTS))
#define CONFIG_LOG_TRACE 1
#else
#define CONFIG_LOG_TRACE 0
#endif
#endif

#endif // CONFIG_H
