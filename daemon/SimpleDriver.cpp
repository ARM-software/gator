/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#include "SimpleDriver.h"

#include "Counter.h"

SimpleDriver::~SimpleDriver()
{
    DriverCounter * counters = mCounters;
    while (counters != nullptr) {
        DriverCounter * counter = counters;
        counters = counter->getNext();
        delete counter;
    }
}

bool SimpleDriver::claimCounter(Counter & counter) const
{
    return findCounter(counter) != nullptr;
}

bool SimpleDriver::countersEnabled() const
{
    for (DriverCounter * counter = mCounters; counter != nullptr; counter = counter->getNext()) {
        if (counter->isEnabled()) {
            return true;
        }
    }
    return false;
}

void SimpleDriver::resetCounters()
{
    for (DriverCounter * counter = mCounters; counter != nullptr; counter = counter->getNext()) {
        counter->setEnabled(false);
    }
}

void SimpleDriver::setupCounter(Counter & counter)
{
    DriverCounter * const driverCounter = findCounter(counter);
    if (driverCounter == nullptr) {
        counter.setEnabled(false);
        return;
    }
    driverCounter->setEnabled(true);
    counter.setKey(driverCounter->getKey());
}

int SimpleDriver::writeCounters(mxml_node_t * root) const
{
    int count = 0;
    for (DriverCounter * counter = mCounters; counter != nullptr; counter = counter->getNext()) {
        mxml_node_t * node = mxmlNewElement(root, "counter");
        mxmlElementSetAttr(node, "name", counter->getName());
        ++count;
    }

    return count;
}

DriverCounter * SimpleDriver::findCounter(Counter & counter) const
{
    DriverCounter * dcounter = nullptr;
    for (DriverCounter * driverCounter = mCounters; driverCounter != nullptr;
         driverCounter = driverCounter->getNext()) {
        if (strcasecmp(driverCounter->getName(), counter.getType()) == 0) {
            dcounter = driverCounter;
            counter.setType(driverCounter->getName());
            break;
        }
        else {
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
