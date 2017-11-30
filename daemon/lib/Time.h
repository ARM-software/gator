/**
 * Copyright (C) Arm Limited 2017. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

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
