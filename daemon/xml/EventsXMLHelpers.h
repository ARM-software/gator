/* Copyright (C) 2024 by Arm Limited. All rights reserved. */

#pragma once

#include "EventCode.h"
#include "lib/Span.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Driver;
class GatorCpu;
class UncorePmu;

namespace events_xml {

    struct EventCategory;

    struct EventDescriptor {
        std::reference_wrapper<EventCategory const> category;
        std::string id;
        std::string title;
        std::string name;
        std::string description;
        EventCode eventCode;
        bool uses_option_set;

        EventDescriptor(EventCategory const & category,
                        std::string id,
                        std::string title,
                        std::string name,
                        std::string description,
                        EventCode eventCode,
                        bool uses_option_set)
            : category(category),
              id(std::move(id)),
              title(std::move(title)),
              name(std::move(name)),
              description(std::move(description)),
              eventCode(std::move(eventCode)),
              uses_option_set(uses_option_set)
        {
        }
    };

    struct EventCategory {
        std::string name;
        std::string counter_set;
        GatorCpu const * cluster;
        UncorePmu const * uncore;
        std::vector<std::unique_ptr<EventDescriptor>> events {};
        bool contains_metrics = false;

        EventCategory(std::string name, std::string counter_set, GatorCpu const * cluster, UncorePmu const * uncore)
            : name(std::move(name)), counter_set(std::move(counter_set)), cluster(cluster), uncore(uncore)
        {
        }
    };

    struct EventsContents {
        std::vector<std::unique_ptr<EventCategory>> categories {};
        std::map<std::string, std::reference_wrapper<EventDescriptor const>> named_events {};
    };

    [[nodiscard]] EventsContents getEventDescriptors(lib::Span<const Driver * const> drivers,
                                                     lib::Span<const GatorCpu> clusters,
                                                     lib::Span<const UncorePmu> uncores);

    [[nodiscard]] std::map<std::string, EventCode> getCounterToEventMap(lib::Span<const Driver * const> drivers,
                                                                        lib::Span<const GatorCpu> clusters,
                                                                        lib::Span<const UncorePmu> uncores);

    [[nodiscard]] std::string_view trim_cnt_suffix(std::string_view id);

    [[nodiscard]] bool is_same_cset(std::string_view a, std::string_view b);

    [[nodiscard]] events_xml::EventCategory const * find_category_for_cset(
        events_xml::EventsContents const & events_contents,
        std::string const & cset);
};
