/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#include "DriverCounter.h"

#include "GetEventKey.h"

DriverCounter::DriverCounter(DriverCounter * const next, const char * const name)
    : mNext(next), mName(name), mKey(getEventKey()), mEnabled(false)
{
}
