/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#include "PolledDriver.h"

#include "Buffer.h"

PolledDriver::~PolledDriver() {}

void PolledDriver::read(Buffer * const buffer)
{
    for (DriverCounter * counter = getCounters(); counter != NULL; counter = counter->getNext()) {
        if (!counter->isEnabled()) {
            continue;
        }
        buffer->event64(counter->getKey(), counter->read());
    }
}
