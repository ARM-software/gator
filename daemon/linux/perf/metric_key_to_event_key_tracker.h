/* Copyright (C) 2022-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "linux/perf/IPerfAttrsConsumer.h"

#include <cstdint>

class metric_key_to_event_key_tracker_t {
public:
    using metric_event_type_t = IPerfAttrsConsumer::MetricEventType;

    explicit constexpr metric_key_to_event_key_tracker_t(IPerfAttrsConsumer & consumer) : consumer(consumer) {}

    void operator()(int metric_key, std::uint16_t event, int event_key, metric_event_type_t event_type)
    {
        consumer.marshalMetricKey(metric_key, event, event_key, event_type);
    }

private:
    IPerfAttrsConsumer & consumer;
};
