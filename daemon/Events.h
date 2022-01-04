/* Copyright (C) 2020-2021 by Arm Limited. All rights reserved. */

#pragma once

#include "EventCode.h"

#include <optional>
#include <string>
#include <vector>

struct Event {
    enum Class { DELTA, INCIDENT, ABSOLUTE, ACTIVITY };

    // at least one of eventNumber or counter should be present
    EventCode eventNumber;
    std::optional<std::string> counter;
    Class clazz;
    double multiplier;
    std::string name;
    std::string title;
    std::string description;
    std::string units;
};

struct CounterSet {
    std::string name;
    int count;
};

struct Category {
    std::string name;
    std::optional<CounterSet> counterSet;
    std::vector<Event> events;
};
