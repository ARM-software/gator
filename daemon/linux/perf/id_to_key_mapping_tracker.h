/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "k/perf_event.h" // Use a snapshot of perf_event.h as it may be more recent than what is on the target and if not newer features won't be supported anyways
#include "linux/perf/IPerfAttrsConsumer.h"

/**
 * Wrap the IPerfAttrsConsumer, calling marshalKeys/marshalKeysOld. Later this will just accumulate the values into a vector to allow being sent later via some async message.
 */
class id_to_key_mapping_tracker_t {
public:
    explicit constexpr id_to_key_mapping_tracker_t(IPerfAttrsConsumer & consumer) : consumer(consumer) {}

    void operator()(int count, const uint64_t * ids, const int * keys) { consumer.marshalKeys(count, ids, keys); }

    void operator()(int keyCount, const int * keys, int bytes, const char * buf)
    {
        consumer.marshalKeysOld(keyCount, keys, bytes, buf);
    }

private:
    IPerfAttrsConsumer & consumer;
};
