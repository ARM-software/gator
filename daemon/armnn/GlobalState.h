/* Copyright (C) 2019-2021 by Arm Limited. All rights reserved. */
#pragma once

#include "Events.h"
#include "armnn/IGlobalState.h"
#include "lib/SharedMemory.h"
#include "lib/StaticVector.h"

#include <functional>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace armnn {
    class GlobalState : public IGlobalState {
    public:
        /**
         * @param keyAllocator (will only be used in main thread of child)
         */
        GlobalState(std::function<int(void)> keyAllocator,
                    std::function<std::size_t(std::string)> nameHasher = std::hash<std::string> {})
            : keyAllocator {std::move(keyAllocator)}, nameHasher {std::move(nameHasher)}
        {
        }

        // ------------- IGlobalState --------------------- //
        // multithread safe

        /** @return A map from global event id to APC counter key */
        EventKeyMap getRequestedCounters() const override;
        /** @return The requested capture mode */
        CaptureMode getCaptureMode() const override;
        /** @return The requested sample period in microseconds */
        std::uint32_t getSamplePeriod() const override;

        void addEvents(std::vector<std::tuple<EventId, EventProperties>> /*unused*/) override;

        // not multithread safe (but it doesn't matter because they're all called by
        // the main thread of gator child before capture sessions are started)
        std::vector<Category> getCategories() const;

        bool hasCounter(const std::string & counterName) const;

        ///
        /// @param eventNumber should be empty if not a programmable counter
        /// @return key assigned for counter
        ///
        int enableCounter(const std::string & counterName, EventCode eventNumber);

        void disableAllCounters();

        std::vector<std::string> getAllCounterNames() const;

    private:
        struct CategoryId {
            std::string category;
            std::optional<std::string> device;
            std::optional<std::string> counterSet;

            static CategoryId fromEventId(const EventId & eventId);

            bool operator<(const CategoryId & that) const;

            std::string toXmlName() const;
        };

        struct CategoryEvents {
            std::map<std::string, std::optional<EventProperties>> events;
            std::uint16_t counterSetCount;
            std::map<int, std::string> eventsByNumber;
        };

        struct CounterNameKeyAndEventNumber {
            const std::string & counterName;
            int key;
            EventCode eventNumber;
        };

        static std::string eventIdToString(const EventId & id);
        static std::string makeCounterNamePrefix(const CategoryId & id);
        static std::string makeCounterSetName(const CategoryId & id);
        static std::string makeCounterSetCounterName(const CategoryId & id, int counterNumber);
        static std::string makeEventCounterName(const CategoryId & id, const std::string & eventName);
        static EventId makeEventId(const CategoryId & id, const std::string & eventName);
        static std::optional<std::string> makeCounterNameIfFixed(const CategoryId & id, const std::string & eventName);
        static std::optional<CounterSet> makeCounterSet(const CategoryId & catId,
                                                        const CategoryEvents & categoryEvents);
        static std::vector<Event> createXmlEvents(const CategoryId & catId, const CategoryEvents & category);

        void insertEventNumber(std::map<int, std::string> & eventNumberToName, const std::string & name) const;

        int getKey(const std::string & counterName);
        void addEvent(const EventId & id, const EventProperties & properties);
        CategoryEvents & getOrCreateCategory(const CategoryId &, std::uint16_t counterSetCount);

        std::function<int(void)> keyAllocator;
        std::function<std::size_t(std::string)> nameHasher;
        mutable std::mutex eventsMutex {};
        std::map<std::string, int> keysByCounterName {};
        std::map<CategoryId, CategoryEvents> categories {};
        std::map<std::string, EventId> fixedCountersToEvent {};
        std::map<std::string, CategoryId> programmableCountersToCategory {};
        // Any references to elements in this set will be valid forever if they're not removed
        // so only references added before fork should be passed to the other process
        std::set<std::string> permanentCounterNameReferences {};
        // StaticVector and IdKeyAndEventNumber don't dynamically allocate so we can safely use them in shared memory
        shared_memory::unique_ptr<lib::StaticVector<CounterNameKeyAndEventNumber, 1000>> enabledIdKeyAndEventNumbers =
            shared_memory::make_unique<lib::StaticVector<CounterNameKeyAndEventNumber, 1000>>();
    };

}
