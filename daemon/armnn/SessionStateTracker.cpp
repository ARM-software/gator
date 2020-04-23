/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#include "armnn/SessionStateTracker.h"

#include "Logging.h"
#include "armnn/CounterDirectoryStateUtils.h"
#include <utility>

namespace armnn {

    SessionStateTracker::~SessionStateTracker()
    {
        globalState.removeSessionState(this);
    }

    bool SessionStateTracker::insertRequested(
        std::map<std::uint16_t, ApcCounterKeyAndCoreNumber> & newRequestedEventUIDs,
        std::set<std::uint16_t> & newActiveEventUIDs,
        bool captureIsActive,
        int key,
        const CategoryRecord & cat,
        const EventRecord & event)
    {
        for (std::uint32_t i = event.uid; i <= event.max_uid; ++i) {
            const std::uint32_t core = (i - event.uid);
            if (!newRequestedEventUIDs.emplace(i, ApcCounterKeyAndCoreNumber{key, core}).second) {
                return false;
            }

            if (captureIsActive && !newActiveEventUIDs.insert(i).second) {
                return false;
            }
        }

        return true;
    }

    SessionStateTracker::SessionStateTracker(IGlobalState & globalState,
                                             IGlobalCounterConsumer & globalCounterConsumer,
                                             ISessionPacketSender & sendQueue)
        : globalState(globalState),
          globalCounterConsumer(globalCounterConsumer),
          sendQueue(sendQueue),
          mutex(),
          availableCounterDirectoryDevices(),
          availableCounterDirectoryCounterSets(),
          availableCounterDirectoryCategories(),
          globalIdToCategoryAndEvent(),
          requestedEventUIDs(),
          activeEventUIDs()
    {
    }

    bool SessionStateTracker::onCounterDirectory(std::map<std::uint16_t, DeviceRecord> devices,
                                                 std::map<std::uint16_t, CounterSetRecord> counterSets,
                                                 std::vector<CategoryRecord> categories)
    {
        std::set<std::uint16_t> seenUids;
        std::map<armnn::EventId, CategoryIndexEventUID> newGlobalIdToCategoryAndEvent;
        std::map<std::uint16_t, ApcCounterKeyAndCoreNumber> newRequestedEventUIDs;
        std::set<std::uint16_t> newActiveEventUIDs;

        const bool captureIsActive = globalState.isCaptureActive();
        const std::map<armnn::EventId, int> & requestedEvents = globalState.getRequestedCounters();
        const CaptureMode captureMode = globalState.getCaptureMode();
        const std::uint32_t samplePeriod = globalState.getSamplePeriod();

        // first validate the data - make sure that devices / countersets references are correct
        for (std::size_t i = 0; i < categories.size(); ++i) {
            const auto & cat = categories.at(i);
            if ((cat.device_uid != 0) && (devices.count(cat.device_uid) == 0)) {
                logg.logError("Invalid counter directory, category '%s' references invalid device 0x%04x",
                              cat.name.c_str(),
                              cat.device_uid);
                return false;
            }
            if ((cat.counter_set_uid != 0) && (counterSets.count(cat.counter_set_uid) == 0)) {
                logg.logError("Invalid counter directory, category '%s' references invalid counter set 0x%04x",
                              cat.name.c_str(),
                              cat.device_uid);
                return false;
            }
            for (const auto & epair : cat.events_by_uid) {
                const auto & event = epair.second;
                if ((event.device_uid != 0) && (devices.count(event.device_uid) == 0)) {
                    logg.logError(
                        "Invalid counter directory, event '%s'.'%s' (0x%04x) references invalid device 0x%04x",
                        cat.name.c_str(),
                        event.name.c_str(),
                        event.uid,
                        event.device_uid);
                    return false;
                }
                if ((event.counter_set_uid != 0) && (counterSets.count(event.counter_set_uid) == 0)) {
                    logg.logError(
                        "Invalid counter directory, event '%s'.'%s' (0x%04x) references invalid counter set 0x%04x",
                        cat.name.c_str(),
                        event.name.c_str(),
                        event.uid,
                        event.counter_set_uid);
                    return false;
                }

                // track/validate UIDs (they should be unique)
                for (std::uint32_t i = event.uid; i <= event.max_uid; ++i) {
                    if (!seenUids.insert(i).second) {
                        logg.logError("Invalid counter directory, event '%s'.'%s' (0x%04x) overlaps another event with "
                                      "the same UID",
                                      cat.name.c_str(),
                                      event.name.c_str(),
                                      event.uid);
                        return false;
                    }
                }

                // map to globalId
                armnn::EventId globalId = makeEventId(devices, counterSets, cat, event);

                // check is requested event, add to lookup if it is
                auto rIt = requestedEvents.find(globalId);
                if (rIt != requestedEvents.end()) {
                    const int key = rIt->second;
                    if (!insertRequested(newRequestedEventUIDs, newActiveEventUIDs, captureIsActive, key, cat, event)) {
                        logg.logError("Invalid counter directory, unexpected issue with event '%s'.'%s' (0x%04x)",
                                      cat.name.c_str(),
                                      event.name.c_str(),
                                      event.uid);
                        return false;
                    }
                }

                // update global id -> cat/event map
                if (!newGlobalIdToCategoryAndEvent.emplace(std::move(globalId), CategoryIndexEventUID{i, event.uid})
                         .second) {
                    logg.logError("Invalid counter directory, event '%s'.'%s' (0x%04x) overlaps another event with the "
                                  "same global id",
                                  cat.name.c_str(),
                                  event.name.c_str(),
                                  event.uid);
                    return false;
                }
            }
        }

        // lock modification of state
        std::lock_guard<std::mutex> lock{mutex};

        // update this object
        availableCounterDirectoryDevices = std::move(devices);
        availableCounterDirectoryCounterSets = std::move(counterSets);
        availableCounterDirectoryCategories = std::move(categories);

        globalIdToCategoryAndEvent = std::move(newGlobalIdToCategoryAndEvent);
        requestedEventUIDs = std::move(newRequestedEventUIDs);
        // don't update activeEventUIDs, that will happen on the call back from requestActivateCounterSelection


        updateGlobalWithNewCategories(
            globalIdToCategoryAndEvent,
            availableCounterDirectoryCategories,
            availableCounterDirectoryDevices,
            availableCounterDirectoryCounterSets
            );

        // Send request to ArmNN to update active events
        if (captureIsActive) {
            return sendQueue.requestActivateCounterSelection(captureMode, samplePeriod, newActiveEventUIDs);
        }
        else {
            return true;
        }
    }

    bool SessionStateTracker::onPeriodicCounterSelection(std::uint32_t period, std::set<std::uint16_t> uids)
    {
        // lock modification of state
        std::lock_guard<std::mutex> lock{mutex};

        // update list
        activeEventUIDs = std::move(uids);

        return true;
    }

    bool SessionStateTracker::onPerJobCounterSelection(std::uint64_t objectId, std::set<std::uint16_t> uids)
    {
        // lock modification of state
        std::lock_guard<std::mutex> lock{mutex};

        // update list
        activeEventUIDs = std::move(uids);

        return true;
    }

    bool SessionStateTracker::onPeriodicCounterCapture(std::uint64_t timestamp,
                                                       std::map<std::uint16_t, std::uint32_t> counterIndexValues)
    {
        std::lock_guard<std::mutex> lock{mutex};
        for (const auto & uidAndValue : counterIndexValues) {
            auto match = requestedEventUIDs.find(uidAndValue.first);
            if (match != requestedEventUIDs.end()) {
                if (!globalCounterConsumer.consumerCounterValue(timestamp, match->second, uidAndValue.second))
                    return false;
            }
        }
        return true;
    }

    bool SessionStateTracker::onPerJobCounterCapture(bool isPre,
                                                     std::uint64_t timestamp,
                                                     std::uint64_t objectRef,
                                                     std::map<std::uint16_t, std::uint32_t> counterIndexValues)
    {
        // TODO: what about isPre and objectRef
        return onPeriodicCounterCapture(timestamp, counterIndexValues);
    }

    bool SessionStateTracker::doRequestEnableEvents(const std::map<armnn::EventId, int> & eventIdAndKey)
    {
        std::map<std::uint16_t, ApcCounterKeyAndCoreNumber> newRequestedEventUIDs;
        std::set<std::uint16_t> newActiveEventUIDs;

        const bool captureIsActive = globalState.isCaptureActive();
        const CaptureMode captureMode = globalState.getCaptureMode();
        const std::uint32_t samplePeriod = globalState.getSamplePeriod();

        // lock modification of state
        std::lock_guard<std::mutex> lock{mutex};

        // iterate each item in eventIdAndKey
        for (const auto & pair : eventIdAndKey) {
            const armnn::EventId & globalId = pair.first;
            const int key = pair.second;

            // find matching item in globalIdToCategoryAndEvent
            const auto it = globalIdToCategoryAndEvent.find(globalId);
            if (it == globalIdToCategoryAndEvent.end()) {
                continue;
            }

            // find category and event
            const auto & category = availableCounterDirectoryCategories.at(it->second.index);
            const auto & event = category.events_by_uid.at(it->second.uid);

            // add to requested map
            if (!insertRequested(newRequestedEventUIDs, newActiveEventUIDs, captureIsActive, key, category, event)) {
                logg.logError("Failed to update requested counters, unexpected issue with event '%s'.'%s' (0x%04x)",
                              category.name.c_str(),
                              event.name.c_str(),
                              event.uid);
                return false;
            }
        }

        // update this object
        requestedEventUIDs = std::move(newRequestedEventUIDs);
        // don't update activeEventUIDs, that will happen on the call back from requestActivateCounterSelection

        // Send request to ArmNN to update active events
        if (captureIsActive) {
            return sendQueue.requestActivateCounterSelection(captureMode, samplePeriod, newActiveEventUIDs);
        }
        else {
            return true;
        }
    }

    bool SessionStateTracker::doEnableCapture()
    {
        std::set<std::uint16_t> newActiveEventUIDs;

        const CaptureMode captureMode = globalState.getCaptureMode();
        const std::uint32_t samplePeriod = globalState.getSamplePeriod();

        // lock modification of state
        std::lock_guard<std::mutex> lock{mutex};

        for (const auto & pair : requestedEventUIDs) {
            if (!newActiveEventUIDs.insert(pair.first).second) {
                logg.logError("Failed to update active counters, unexpected issue with event (0x%04x)", pair.first);
                return false;
            }
        }

        // don't update activeEventUIDs, that will happen on the call back from requestActivateCounterSelection

        // Send request to ArmNN to update active events
        return sendQueue.requestActivateCounterSelection(captureMode, samplePeriod, newActiveEventUIDs);
    }

    bool SessionStateTracker::doDisableCapture()
    {
        // lock modification of state
        std::lock_guard<std::mutex> lock{mutex};

        // clear the list of active items, even before we receive the counter selection packet
        activeEventUIDs.clear();

        // Send request to ArmNN to update active events
        return sendQueue.requestDisableCounterSelection();
    }

    EventId SessionStateTracker::makeEventId(
        const std::map<std::uint16_t, ICounterDirectoryConsumer::DeviceRecord> & deviceMap,
        const std::map<std::uint16_t, ICounterDirectoryConsumer::CounterSetRecord> & counterSetMap,
        const ICounterDirectoryConsumer::CategoryRecord & category,
        const ICounterDirectoryConsumer::EventRecord & record)
    {

        EventId id {};
        id.category = category.name;
        id.name = record.name;
        if (record.device_uid > 0)
        {
            const ICounterDirectoryConsumer::DeviceRecord & dr = deviceMap.at(record.device_uid);
            id.device = lib::Optional<std::string>(dr.name);
        }
        if (record.counter_set_uid > 0)
        {
            const ICounterDirectoryConsumer::CounterSetRecord & csr = counterSetMap.at(record.counter_set_uid);
            id.counterSet = lib::Optional<std::string>(csr.name);
        }
        return id;
    }

    void SessionStateTracker::updateGlobalWithNewCategories(
            const std::map<EventId, CategoryIndexEventUID> & newGlobalIdToCategoryAndEvent,
            const std::vector<CategoryRecord> & categories,
            const std::map<std::uint16_t, DeviceRecord> & devicesById,
            const std::map<std::uint16_t, CounterSetRecord> & counterSetsById
            )
    {
        std::vector<std::tuple<armnn::EventId, armnn::EventProperties>> data { };
        for (auto i : newGlobalIdToCategoryAndEvent)
        {
            const auto & cr = categories.at(i.second.index);
            const auto & event = cr.events_by_uid.at(i.second.uid);
            lib::Optional<std::string> deviceOpt;
            lib::Optional<std::string> counterSetOpt;
            lib::Optional<std::uint16_t> counterSetCount;
            if (event.device_uid > 0)
            {
                deviceOpt.set(devicesById.at(event.device_uid).name);
            }
            if (event.counter_set_uid > 0)
            {
                const auto & csrecord = counterSetsById.at(event.counter_set_uid);
                counterSetOpt.set(csrecord.name);
                counterSetCount.set(csrecord.count);
            }

            armnn::EventId eventId {cr.name, deviceOpt, counterSetOpt, event.name};
            armnn::EventProperties eventProperties {(counterSetCount ? counterSetCount.get() : std::uint16_t{0}),
                 event.clazz, event.interpolation, event.multiplier, event.description, event.units};
            std::tuple<armnn::EventId, armnn::EventProperties> tuple {std::move(eventId), std::move(eventProperties)};

            data.emplace_back(std::move(tuple));
        }

        globalState.setSessionEvents(this, std::move(data));
    }
}
