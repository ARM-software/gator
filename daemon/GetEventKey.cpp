/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#include "GetEventKey.h"

CounterKey getEventKey()
{
    // key 0 is reserved as a timestamp
    // key 1 is reserved as the marker for thread specific counters
    // key 2 is reserved as the marker for core
    // Odd keys are assigned by the driver, even keys by the daemon
    static CounterKey key = 4;

    const CounterKey ret = key;
    key += 2;
    return ret;
}
