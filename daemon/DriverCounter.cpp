/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "DriverCounter.h"
#include "SessionData.h"

DriverCounter::DriverCounter(DriverCounter * const next, const char * const name)
        : mNext(next),
          mName(name),
          mKey(getEventKey()),
          mEnabled(false)
{
}
