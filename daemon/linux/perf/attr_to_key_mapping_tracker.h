/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "k/perf_event.h" // Use a snapshot of perf_event.h as it may be more recent than what is on the target and if not newer features won't be supported anyways
#include "linux/perf/IPerfAttrsConsumer.h"

/**
 * Wrap the IPerfAttrsConsumer, calling marshalPea. Later this will just accumulate the values into a vector to allow being sent later via some async message.
 */
class attr_to_key_mapping_tracker_t {
public:
    explicit constexpr attr_to_key_mapping_tracker_t(IPerfAttrsConsumer & consumer) : consumer(consumer) {}

    void operator()(int key, perf_event_attr const & attr) { consumer.marshalPea(&attr, key); }

private:
    IPerfAttrsConsumer & consumer;
};
