/* Copyright (C) 2024 by Arm Limited. All rights reserved. */

#include "xml/EventsXMLHelpers.h"

#include "Driver.h"
#include "EventCode.h"
#include "lib/Span.h"
#include "xml/EventsXML.h"
#include "xml/PmuXML.h"

#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include <mxml.h>

namespace events_xml {
    namespace {
        template<typename T>
        [[nodiscard]] T const * get_category_pmu(std::string_view counter_set, lib::Span<const T> pmus)
        {
            if (counter_set.empty()) {
                return nullptr;
            }

            for (auto const & pmu : pmus) {
                std::string_view const id = pmu.getId();
                std::string_view const cs = pmu.getCounterSet();

                if (is_same_cset(counter_set, id) || is_same_cset(counter_set, cs)) {
                    return &pmu;
                }
            }

            return nullptr;
        }

        void add_one_event(EventsContents & result, EventCategory & category, mxml_node_t * event_node)
        {
            const char * event_id = mxmlElementGetAttr(event_node, "counter");
            const char * event_title = mxmlElementGetAttr(event_node, "title");
            const char * event_name = mxmlElementGetAttr(event_node, "name");
            const char * event_description = mxmlElementGetAttr(event_node, "description");
            const char * event_code = mxmlElementGetAttr(event_node, "event");
            const char * event_option_set = mxmlElementGetAttr(event_node, "option_set");
            const char * event_metric = mxmlElementGetAttr(event_node, "metric");

            EventCode code {};

            if (event_code != nullptr) {
                const auto eventNo = strtoull(event_code, nullptr, 0);
                code = EventCode(eventNo);
            }

            auto const event_option_set_str = (event_option_set != nullptr ? std::string_view(event_option_set) //
                                                                           : std::string_view());

            auto const event_metric_str = (event_metric != nullptr ? std::string_view(event_metric) //
                                                                   : std::string_view());
            auto const & event_ptr = category.events.emplace_back(
                std::make_unique<EventDescriptor>(category,
                                                  (event_id != nullptr ? event_id : ""),
                                                  (event_title != nullptr ? event_title : ""),
                                                  (event_name != nullptr ? event_name : ""),
                                                  (event_description != nullptr ? event_description : ""),
                                                  code,
                                                  !event_option_set_str.empty()));

            if (!event_ptr->id.empty()) {
                result.named_events.try_emplace(event_ptr->id, *event_ptr);
            }

            if (event_metric_str == "yes") {
                category.contains_metrics = true;
            }
        }
    }

    EventsContents getEventDescriptors(lib::Span<const Driver * const> drivers,
                                       lib::Span<const GatorCpu> clusters,
                                       lib::Span<const UncorePmu> uncores)
    {
        auto xml = getDynamicTree(drivers, clusters, uncores);

        EventsContents result {};

        // first parse all the categories
        for (auto * category_node = mxmlFindElement(xml.get(), xml.get(), "category", nullptr, nullptr, MXML_DESCEND);
             category_node != nullptr;
             category_node = mxmlFindElement(category_node, xml.get(), "category", nullptr, nullptr, MXML_DESCEND)) {

            char const * category_name = mxmlElementGetAttr(category_node, "name");
            char const * category_counter_set = mxmlElementGetAttr(category_node, "counter_set");

            auto & category_ptr = result.categories.emplace_back(std::make_unique<EventCategory>(
                (category_name != nullptr ? category_name : ""),
                (category_counter_set != nullptr ? category_counter_set : ""),
                (category_counter_set != nullptr ? get_category_pmu(category_counter_set, clusters) : nullptr),
                (category_counter_set != nullptr ? get_category_pmu(category_counter_set, uncores) : nullptr)));
            auto & category = *category_ptr;

            // and for each category parse its events
            for (auto * event_node =
                     mxmlFindElement(category_node, category_node, "event", nullptr, nullptr, MXML_DESCEND);
                 event_node != nullptr;
                 event_node = mxmlFindElement(event_node, category_node, "event", nullptr, nullptr, MXML_DESCEND)) {

                add_one_event(result, category, event_node);
            }
        }

        return result;
    }

    std::map<std::string, EventCode> getCounterToEventMap(lib::Span<const Driver * const> drivers,
                                                          lib::Span<const GatorCpu> clusters,
                                                          lib::Span<const UncorePmu> uncores)
    {
        std::map<std::string, EventCode> counterToEventMap {};

        auto xml = getDynamicTree(drivers, clusters, uncores);

        // build map of counter->event
        mxml_node_t * node = xml.get();
        while (true) {
            node = mxmlFindElement(node, xml.get(), "event", nullptr, nullptr, MXML_DESCEND);
            if (node == nullptr) {
                break;
            }
            const char * counter = mxmlElementGetAttr(node, "counter");
            const char * event = mxmlElementGetAttr(node, "event");
            if (counter == nullptr) {
                continue;
            }

            if (event != nullptr) {
                const auto eventNo = strtoull(event, nullptr, 0);
                counterToEventMap[counter] = EventCode(eventNo);
            }
            else {
                counterToEventMap[counter] = EventCode();
            }
        }
        return counterToEventMap;
    }

    std::string_view trim_cnt_suffix(std::string_view id)
    {
        auto const n = id.length();

        if (n < 4) {
            return id;
        }

        if ((id[n - 4] == '_') && (id[n - 3] == 'c') && (id[n - 2] == 'n') && (id[n - 1] == 't')) {
            return id.substr(0, n - 4);
        }

        return id;
    }

    bool is_same_cset(std::string_view a, std::string_view b)
    {
        a = trim_cnt_suffix(a);
        b = trim_cnt_suffix(b);

        return a == b;
    }

    EventCategory const * find_category_for_cset(EventsContents const & events_contents, std::string const & cset)
    {
        for (auto const & category_ptr : events_contents.categories) {
            if ((!category_ptr->counter_set.empty()) && is_same_cset(category_ptr->counter_set, cset)) {
                return category_ptr.get();
            }
        }

        return nullptr;
    }
}
