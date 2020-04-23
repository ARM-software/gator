/* Copyright (C) 2017-2020 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_TIME_H
#define INCLUDE_LIB_TIME_H

#include <time.h>

// some toolchains appear not to provide these defines, manually provide it as they supported on all
// kernel versions supported by gatord (they are both since Linux 2.6.39)
#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW 4
#endif
#ifndef CLOCK_BOOTTIME
#define CLOCK_BOOTTIME 7
#endif

#endif // INCLUDE_LIB_TIME_H
