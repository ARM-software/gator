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



bool counterNameFuzzyEquals(const char * properName, const char * fuzzyName) {
    if (strcasecmp(properName, fuzzyName) == 0) {
        return true;
    } else {
        //to get the slot name when only part of the counter name is given
        //for eg: ARMv8_Cortex_A53 --> should be read as ARMv8_Cortex_A53_cnt0
        std::string counterType = fuzzyName;
        counterType += "_cnt";
        if (strncasecmp(properName, counterType.c_str(), counterType.length()) == 0) {
            return true;
        } else {
            return false;
        }
    }
}

DriverCounter *SimpleDriver::findCounter(Counter &counter) const
{
    for (DriverCounter *driverCounter = mCounters; driverCounter != NULL; driverCounter = driverCounter->getNext()) {
        if (counterNameFuzzyEquals(driverCounter->getName(), counter.getType())) {
            counter.setType(driverCounter->getName());
            return driverCounter;
        }
    }
    return nullptr;
}

