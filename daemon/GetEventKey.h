/* Copyright (C) 2020-2022 by Arm Limited. All rights reserved. */

#ifndef GET_EVENT_KEY_H
#define GET_EVENT_KEY_H

using CounterKey = int;

// key 0 is reserved as a timestamp
// key 1 is reserved as the marker for thread specific counters
// key 2 is reserved as the marker for core
// Odd keys are assigned by the driver, even keys by the daemon
static constexpr CounterKey magic_key_timestamp = 0;
static constexpr CounterKey magic_key_tid = 1;
static constexpr CounterKey magic_key_core = 2;
static constexpr CounterKey first_free_key = 4;

CounterKey getEventKey();

#endif // GET_EVENT_KEY_H
