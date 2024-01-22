/* Copyright (C) 2013-2023 by Arm Limited. All rights reserved. */

#include "PolledDriver.h"

#include "DriverCounter.h"
#include "IBlockCounterFrameBuilder.h"

void PolledDriver::read(IBlockCounterFrameBuilder & buffer)
{
    for (DriverCounter * counter = getCounters(); counter != nullptr; counter = counter->getNext()) {
        if (!counter->isEnabled()) {
            continue;
        }
        buffer.event64(counter->getKey(), counter->read());
    }
}
