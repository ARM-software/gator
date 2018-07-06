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
#define ACCESS_ONCE(x)  (*reinterpret_cast<volatile __typeof(x) *>(&(x)))

#define MAX_PERFORMANCE_COUNTERS 100
#define NR_CPUS 128
#define CLUSTER_COUNT 4
#define CLUSTER_MASK (CLUSTER_COUNT - 1)

// If debugfs is not mounted at /sys/kernel/debug, update TRACING_PATH
#define TRACING_PATH "/sys/kernel/debug/tracing"
#define EVENTS_PATH TRACING_PATH "/events"

#endif // CONFIG_H
