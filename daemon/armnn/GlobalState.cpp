/* Copyright (C) 2020-2024 by Arm Limited. All rights reserved. */

#include "armnn/GlobalState.h"

#include "EventCode.h"
#include "Events.h"
#include "Logging.h"
#include "armnn/CaptureMode.h"
#include "armnn/ICounterDirectoryConsumer.h"
#include "armnn/IGlobalState.h"
#include "lib/EnumUtils.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace armnn {
    static constexpr std::uint32_t DEFAULT_SAMPLE_PERIOD_MICROS = 1000;
    static void replaceWhitespace(std::string & s)
    {
        for (char & c : s) {
            if (std::isspace(c) != 0) {
                c = '_';
            }
        }
    }

    GlobalState::CategoryId GlobalState::CategoryId::fromEventId(const EventId & eventId)
    {
        return GlobalState::CategoryId {.category = eventId.category,
                                        .device = eventId.device,
                                        .counterSet = eventId.counterSet};
    }

    bool GlobalState::CategoryId::operator<(const CategoryId & that) const
    {
        return std::tie(category, device, counterSet) < std::tie(that.category, that.device, that.counterSet);
    }

    std::string GlobalState::CategoryId::toXmlName() const
    {
        std::string xmlName = category;

        if (device) {
            xmlName += " (";
            xmlName += *device;
            xmlName += ")";
        }

        if (counterSet) {
            xmlName += " [";
            xmlName += *counterSet;
            xmlName += "]";
        }

        return xmlName;
    }

    /** @return A map from global event id to APC counter key */
    std::map<EventId, int> GlobalState::getRequestedCounters() const
    {
        std::lock_guard<std::mutex> lock {eventsMutex};

        std::map<EventId, int> out;

        for (const auto & counterNameKeyAndEventNumber : *enabledIdKeyAndEventNumbers) {
            const auto & counterName = counterNameKeyAndEventNumber.counterName;
            const auto & eventNumber = counterNameKeyAndEventNumber.eventNumber;
            const int key = counterNameKeyAndEventNumber.key;

            if (eventNumber.isValid()) {
                const auto eventByNumberIter = programmableCountersToCategory.find(counterName);
                if (eventByNumberIter != programmableCountersToCategory.end()) {
                    const auto & categoryId = eventByNumberIter->second;
                    const auto & category = categories.at(categoryId);
                    const auto iter = category.eventsByNumber.find(eventNumber.asI32());
                    if (iter != category.eventsByNumber.end()) {
                        const auto & eventName = iter->second;
                        // check it wasn't removed due to conflicting properties
                        if (category.events.at(eventName)) {
                            out.insert({makeEventId(categoryId, eventName), key});
                        }
                    }
                    else {
                        LOG_ERROR("Unknown event number 0x%" PRIxEventCode " for counter: %s",
                                  eventNumber.asU64(),
                                  counterName.c_str());
                    }
                }
                else {
                    LOG_ERROR("Unknown counter: %s", counterName.c_str());
                }
            }
            else {
                const auto iter = fixedCountersToEvent.find(counterName);
                if (iter != fixedCountersToEvent.end()) {
                    const auto & eventId = iter->second;
                    // check it wasn't removed due to conflicting properties
                    if (categories.at(CategoryId::fromEventId(eventId)).events.at(eventId.name)) {
                        out.insert({eventId, key});
                    }
                }
                else {
                    LOG_ERROR("Unknown counter: %s", counterName.c_str());
                }
            }
        }
        return out;
    }

    /** @return The requested capture mode */
    CaptureMode GlobalState::getCaptureMode() const
    {
        return CaptureMode::PERIOD_CAPTURE;
    }
    /** @return The requested sample period in microseconds */
    std::uint32_t GlobalState::getSamplePeriod() const
    {
        return DEFAULT_SAMPLE_PERIOD_MICROS;
    }

    static Event::Class toEventClass(ICounterDirectoryConsumer::Class clazz,
                                     ICounterDirectoryConsumer::Interpolation interpolation)
    {
        switch (clazz) {
            case ICounterDirectoryConsumer::Class::DELTA: {
                switch (interpolation) {
                    case ICounterDirectoryConsumer::Interpolation::LINEAR:
                        return Event::Class::DELTA;
                    case ICounterDirectoryConsumer::Interpolation::STEP:
                        return Event::Class::INCIDENT;
                }
                break;
            }
            case ICounterDirectoryConsumer::Class::ABSOLUTE: {
                switch (interpolation) {
                    // we don't currently support linear interpolation
                    // for absolute, steps will have to do.
                    case ICounterDirectoryConsumer::Interpolation::LINEAR:
                    case ICounterDirectoryConsumer::Interpolation::STEP:
                        return Event::Class::ABSOLUTE;
                }
                break;
            }
        }

        assert(false && "unknown Class/Interpolation");
        return Event::Class::DELTA; // just to keep the compiler happy
    }

    std::string GlobalState::makeCounterNamePrefix(const GlobalState::CategoryId & id)
    {
        std::string name = "ArmNN_" + id.category;

        if (id.device) {
            name += "_";
            name += *id.device;
        }

        if (id.counterSet) {
            name += "_";
            name += *id.counterSet;
        }

        replaceWhitespace(name);

        return name;
    }

    std::string GlobalState::makeCounterSetName(const CategoryId & id)
    {
        return makeCounterNamePrefix(id) + "_cnt";
    }

    std::string GlobalState::makeCounterSetCounterName(const CategoryId & id, int counterNumber)
    {
        return makeCounterSetName(id) + std::to_string(counterNumber);
    }

    std::string GlobalState::makeEventCounterName(const CategoryId & id, const std::string & eventName)
    {
        std::string name = makeCounterNamePrefix(id);

        name += "_";
        name += eventName;

        replaceWhitespace(name);

        return name;
    }

    EventId GlobalState::makeEventId(const CategoryId & id, const std::string & eventName)
    {
        return EventId {.category = id.category, .device = id.device, .counterSet = id.counterSet, .name = eventName};
    }

    /// @return empty if the event doesn't have a counter name (because it's part of a counter set)
    std::optional<std::string> GlobalState::makeCounterNameIfFixed(const GlobalState::CategoryId & id,
                                                                   const std::string & eventName)
    {
        if (id.counterSet) {
            return {};
        }

        return makeEventCounterName(id, eventName);
    }

    std::string GlobalState::eventIdToString(const EventId & id)
    {
        return CategoryId::fromEventId(id).toXmlName() + " - " + id.name;
    }

    static bool checkEventProperties(EventProperties & current, const EventProperties & new_)
    {
        if (current.clazz != new_.clazz) {
            LOG_ERROR("Mismatching class %d vs %d", lib::toEnumValue(current.clazz), lib::toEnumValue(new_.clazz));
            return false;
        }
        if (current.interpolation != new_.interpolation) {
            LOG_ERROR("Mismatching interpolation %d vs %d",
                      lib::toEnumValue(current.interpolation),
                      lib::toEnumValue(new_.interpolation));
            return false;
        }
        if (current.multiplier != new_.multiplier) {
            LOG_ERROR("Mismatching multiplier %f vs %f", current.multiplier, new_.multiplier);
            return false;
        }
        if (current.description != new_.description) {
            LOG_ERROR("Mismatching description %s vs %s", current.description.c_str(), new_.description.c_str());
            return false;
        }
        if (current.units != new_.units) {
            LOG_ERROR("Mismatching units %s vs %s", current.units.c_str(), new_.units.c_str());
            return false;
        }

        return true;
    }

    void GlobalState::insertEventNumber(std::map<uint16_t, std::string> & eventNumberToName,
                                        const std::string & name) const
    {
        // use a hash of the name so it reproducible when events are removed or added
        // just use 16 bits to keep it small in case it need to be compressed
        // this also matches CPU PMU event numbers so we can be sure that we aren't breaking any assumptions
        // like it being non negative
        std::uint16_t eventNumber = static_cast<std::uint16_t>(nameHasher(name));
        // if there's a conflict just increment (will lose exact reproducibility though)
        while (eventNumberToName.count(eventNumber) != 0) {
            ++eventNumber;
        }
        eventNumberToName.insert({eventNumber, name});
    }

    GlobalState::CategoryEvents & GlobalState::getOrCreateCategory(const CategoryId & categoryId,
                                                                   std::uint16_t counterSetCount)
    {
        auto categoryIter = categories.find(categoryId);
        if (categoryIter == categories.end()) {
            if (categoryId.counterSet) {
                for (int i = 0; i < counterSetCount; ++i) {
                    const auto counterName = makeCounterSetCounterName(categoryId, i);
                    permanentCounterNameReferences.insert(counterName);
                    programmableCountersToCategory.insert({counterName, categoryId});
                }
            }
            return categories
                .insert({categoryId, {.events = {}, .counterSetCount = counterSetCount, .eventsByNumber = {}}})
                .first->second;
        }
        auto & category = categoryIter->second;
        if (category.counterSetCount != counterSetCount) {
            std::uint16_t min = std::min(category.counterSetCount, counterSetCount);
            LOG_ERROR("Mismatching counterSetCount %d vs %d, using %d", category.counterSetCount, counterSetCount, min);
            for (int i = min; i < category.counterSetCount; ++i) {
                const auto counterName = makeCounterSetCounterName(categoryId, i);
                // deliberately not removing the permanent counter name reference
                // because then it wouldn't be permanent
                programmableCountersToCategory.erase(counterName);
            }
            category.counterSetCount = min;
        }
        return category;
    }

    /** counter_set is expected to have already been added */
    void GlobalState::addEvent(const EventId & id, const EventProperties & properties)
    {
        auto categoryId = CategoryId::fromEventId(id);
        auto & category = getOrCreateCategory(categoryId, properties.counterSetCount);
        auto & propertiesByName = category.events;
        auto propertiesIter = propertiesByName.find(id.name);
        if (propertiesIter == propertiesByName.end()) {
            propertiesByName.insert({id.name, properties});
            if (!categoryId.counterSet) {
                const auto counterName = makeEventCounterName(categoryId, id.name);
                permanentCounterNameReferences.insert(counterName);
                fixedCountersToEvent.insert({counterName, id});
            }
            else {
                insertEventNumber(category.eventsByNumber, id.name);
            }
        }
        else {
            auto & currentProperties = propertiesIter->second; // will be empty if there was a conflict previously
            if (currentProperties) {
                if (!checkEventProperties(*currentProperties, properties)) {
                    LOG_ERROR("Event (%s) removed due to conflicting property", eventIdToString(id).c_str());
                    currentProperties.reset();
                }
            }
        }
    }

    std::vector<Event> GlobalState::createXmlEvents(const CategoryId & catId, const CategoryEvents & category)
    {
        auto catName = catId.toXmlName();
        std::vector<Event> xmlEvents;

        std::map<std::string, uint16_t> eventNumberByName;
        for (const auto & numberAndName : category.eventsByNumber) {
            eventNumberByName.insert({numberAndName.second, numberAndName.first});
        }
        for (const auto & eventNameAndProperties : category.events) {

            const auto & eventName = eventNameAndProperties.first;
            const auto & properties = eventNameAndProperties.second;
            if (!properties) {
                continue; // removed because of conflict
            }

            EventCode eventNumberOrEmpty;
            if (catId.counterSet) {
                eventNumberOrEmpty = EventCode(eventNumberByName.at(eventName));
            }
            xmlEvents.push_back({.eventNumber = eventNumberOrEmpty,
                                 .counter = makeCounterNameIfFixed(catId, eventName),
                                 .clazz = toEventClass(properties->clazz, properties->interpolation),
                                 .multiplier = properties->multiplier,
                                 .name = eventName,
                                 .title = catName,
                                 .description = properties->description,
                                 .units = properties->units});
        }

        return xmlEvents;
    }

    std::optional<CounterSet> GlobalState::makeCounterSet(const CategoryId & catId,
                                                          const CategoryEvents & categoryEvents)
    {
        if (!catId.counterSet) {
            return {};
        }

        return {{.name = makeCounterSetName(catId), .count = categoryEvents.counterSetCount}};
    }

    void GlobalState::addEvents(std::vector<std::tuple<EventId, EventProperties>> events)
    {
        std::lock_guard<std::mutex> lock {eventsMutex};

        for (auto & event : events) {
            auto & id = std::get<0>(event);
            auto & properties = std::get<1>(event);
            addEvent(id, properties);
        }
    }

    std::vector<Category> GlobalState::getCategories() const
    {
        std::vector<Category> xmlCategories;

        for (const auto & catAndEvents : categories) {
            const auto & catId = catAndEvents.first;
            const auto & events = catAndEvents.second;
            auto catName = catId.toXmlName();
            auto counterSet = makeCounterSet(catId, events);
            auto xmlEvents = createXmlEvents(catId, events);

            xmlCategories.push_back(
                {.name = std::move(catName), .counterSet = std::move(counterSet), .events = std::move(xmlEvents)});
        }

        return xmlCategories;
    }

    int GlobalState::getKey(const std::string & counterName)
    {
        const auto iter = keysByCounterName.find(counterName);
        if (iter != keysByCounterName.end()) {
            return iter->second;
        }

        const int key = keyAllocator();
        keysByCounterName.insert({counterName, key});
        return key;
    }

    bool GlobalState::hasCounter(const std::string & counterName) const
    {
        return fixedCountersToEvent.count(counterName) != 0 || programmableCountersToCategory.count(counterName) != 0;
    }

    int GlobalState::enableCounter(const std::string & counterName, EventCode eventNumber)
    {
        if (enabledIdKeyAndEventNumbers->full()) {
            LOG_ERROR("Could not enable %s, limit of ArmNN counters reached", counterName.c_str());
            return -1;
        }
        const auto iter = permanentCounterNameReferences.find(counterName);
        if (iter == permanentCounterNameReferences.end()) {
            LOG_ERROR("Could not enable %s, unknown counter name", counterName.c_str());
            return -1;
        }

        // me must use this reference instead because it present in gator-main process which reads this
        const std::string & counterNameRef = *iter;

        const int key = getKey(counterName);

        enabledIdKeyAndEventNumbers->push_back(
            CounterNameKeyAndEventNumber {.counterName = counterNameRef, .key = key, .eventNumber = eventNumber});

        return key;
    }

    void GlobalState::disableAllCounters()
    {
        enabledIdKeyAndEventNumbers->clear();
    }

    std::vector<std::string> GlobalState::getAllCounterNames() const
    {
        std::vector<std::string> allCounterNames;
        allCounterNames.reserve(fixedCountersToEvent.size() + programmableCountersToCategory.size());
        for (const auto & counterEventPair : fixedCountersToEvent) {
            allCounterNames.push_back(counterEventPair.first);
        }
        for (const auto & counterCategoryPair : programmableCountersToCategory) {
            allCounterNames.push_back(counterCategoryPair.first);
        }
        return allCounterNames;
    }
}
