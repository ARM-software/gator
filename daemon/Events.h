/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include "lib/Optional.h"

#include <string>
#include <vector>

struct Event {
    enum Class { DELTA, INCIDENT, ABSOLUTE, ACTIVITY };

    // at least one of eventNumber or counter should be present
    lib::Optional<int> eventNumber;
    lib::Optional<std::string> counter;
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
    lib::Optional<CounterSet> counterSet;
    std::vector<Event> events;
};
