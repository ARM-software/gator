/**
 * Copyright (C) ARM Limited 2013-2016. All rights reserved.
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

DriverCounter::~DriverCounter()
{
    delete mName;
}

DriverCounter *SimpleDriver::findCounter(const Counter &counter) const
{
    for (DriverCounter *driverCounter = mCounters; driverCounter != NULL; driverCounter = driverCounter->getNext()) {
        if (strcmp(driverCounter->getName(), counter.getType()) == 0) {
            return driverCounter;
        }
    }

    return NULL;
}

