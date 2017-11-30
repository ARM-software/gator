/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "PolledDriver.h"
#include "Buffer.h"

PolledDriver::~PolledDriver()
{
}

void PolledDriver::read(Buffer * const buffer)
{
    for (DriverCounter *counter = getCounters(); counter != NULL; counter = counter->getNext()) {
        if (!counter->isEnabled()) {
            continue;
        }
        buffer->event64(counter->getKey(), counter->read());
    }
}
