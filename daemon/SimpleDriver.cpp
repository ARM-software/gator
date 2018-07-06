/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "SimpleDriver.h"
#include "Counter.h"

SimpleDriver::~SimpleDriver()
{
    DriverCounter *counters = mCounters;
    while (counters != NULL) {
        DriverCounter *counter = counters;
        counters = counter->getNext();
        delete counter;
    }
}

bool SimpleDriver::claimCounter(Counter &counter) const
{
    return findCounter(counter) != NULL;
}

bool SimpleDriver::countersEnabled() const
{
    for (DriverCounter *counter = mCounters; counter != NULL; counter = counter->getNext()) {
        if (counter->isEnabled()) {
            return true;
        }
    }
    return false;
}

void SimpleDriver::resetCounters()
{
    for (DriverCounter *counter = mCounters; counter != NULL; counter = counter->getNext()) {
        counter->setEnabled(false);
    }
}

void SimpleDriver::setupCounter(Counter &counter)
{
    DriverCounter * const driverCounter = findCounter(counter);
    if (driverCounter == NULL) {
        counter.setEnabled(false);
        return;
    }
    driverCounter->setEnabled(true);
    counter.setKey(driverCounter->getKey());
}

int SimpleDriver::writeCounters(mxml_node_t *root) const
{
    int count = 0;
    for (DriverCounter *counter = mCounters; counter != NULL; counter = counter->getNext()) {
        mxml_node_t *node = mxmlNewElement(root, "counter");
        mxmlElementSetAttr(node, "name", counter->getName());
        ++count;
    }

    return count;
}

