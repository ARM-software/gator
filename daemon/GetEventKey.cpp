/* Copyright (C) 2020-2022 by Arm Limited. All rights reserved. */

#include "GetEventKey.h"

CounterKey getEventKey()
{
    static CounterKey key = first_free_key;

    const CounterKey ret = key;
    key += 2;
    return ret;
}
