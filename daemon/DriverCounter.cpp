/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "DriverCounter.h"
#include "SessionData.h"
#include <string>

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



DriverCounter *SimpleDriver::findCounter(Counter &counter) const
{
    DriverCounter* dcounter = NULL;
    for (DriverCounter *driverCounter = mCounters; driverCounter != NULL; driverCounter = driverCounter->getNext()) {
        if (strcasecmp(driverCounter->getName(), counter.getType()) == 0) {
            dcounter = driverCounter;
            counter.setType(driverCounter->getName());
            break;
        } else {
            //to get the slot name when only part of the counter name is given
            //for eg: ARMv8_Cortex_A53 --> should be read as ARMv8_Cortex_A53_cnt0
            std::string driverCounterName = driverCounter->getName();
            std::string counterType = counter.getType();
            counterType = counterType + "_cnt";
            driverCounterName = driverCounterName.substr(0, counterType.length());
            if (strcasecmp(driverCounterName.c_str(), counterType.c_str()) == 0) {
                dcounter = driverCounter;
                counter.setType(driverCounter->getName());
                break;
            }
        }
    }
    return dcounter;
}

