/* Copyright (C) 2019-2023 by Arm Limited. All rights reserved. */

#include "armnn/SessionStateTracker.h"

#include "Logging.h"
#include "armnn/CaptureMode.h"
#include "armnn/ICounterConsumer.h"
#include "armnn/ICounterDirectoryConsumer.h"
#include "armnn/IGlobalState.h"
#include "armnn/ISessionPacketSender.h"
#include "lib/Span.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace armnn {

    static std::set<uint16_t> keysOf(const std::map<std::uint16_t, ApcCounterKeyAndCoreNumber> & map)
    {
        std::set<std::uint16_t> s;
        for (auto i : map) {
            s.insert(i.first);
        }
        return s;
    }

    static void insertRequested(std::map<std::uint16_t, ApcCounterKeyAndCoreNumber> & newRequestedEventUIDs,
                                int key,
                                const ICounterDirectoryConsumer::EventRecord & event)
    {
        for (std::uint32_t i = event.uid; i <= event.max_uid; ++i) {
            const std::uint32_t core = (i - event.uid);
            const bool wasInserted = newRequestedEventUIDs.emplace(i, ApcCounterKeyAndCoreNumber {key, core}).second;
            if (!wasInserted) {
                // onCounterDirectory validates there is no overlap
                assert(false && "overlapping event UID");
            }
        }
    }

    static EventId makeEventId(
        const std::map<std::uint16_t, ICounterDirectoryConsumer::DeviceRecord> & deviceMap,
        const std::map<std::uint16_t, ICounterDirectoryConsumer::CounterSetRecord> & counterSetMap,
        const ICounterDirectoryConsumer::CategoryRecord & category,
        const ICounterDirectoryConsumer::EventRecord & record)
    {

        EventId id {};
        id.category = category.name;
        id.name = record.name;
        if (record.device_uid > 0) {
            const ICounterDirectoryConsumer::DeviceRecord & dr = deviceMap.at(record.device_uid);
            id.device = std::optional<std::string>(dr.name);
        }
        if (record.counter_set_uid > 0) {
            const ICounterDirectoryConsumer::CounterSetRecord & csr = counterSetMap.at(record.counter_set_uid);
            id.counterSet = std::optional<std::string>(csr.name);
        }
        return id;
    }

    SessionStateTracker::SessionStateTracker(IGlobalState & globalState,
                                             ICounterConsumer & counterConsumer,
                                             std::unique_ptr<ISessionPacketSender> sendQueue,
                                             std::uint32_t sessionID,
                                             std::vector<std::uint8_t> streamMetadata)
        : globalState(globalState),
          counterConsumer(counterConsumer),
          sendQueue(std::move(sendQueue)),
          streamMetadata {std::move(streamMetadata)},
          sessionID {sessionID}
    {
    }

    bool SessionStateTracker::onCounterDirectory(std::map<std::uint16_t, DeviceRecord> devices,
                                                 std::map<std::uint16_t, CounterSetRecord> counterSets,
                                                 std::vector<CategoryRecord> categories)
    {
        std::set<std::uint16_t> seenUids;
        std::map<armnn::EventId, CategoryIndexEventUID> newGlobalIdToCategoryAndEvent;

        // first validate the data - make sure that devices / countersets references are correct
        for (std::size_t i = 0; i < categories.size(); ++i) {
            const auto & cat = categories.at(i);
            for (const auto & epair : cat.events_by_uid) {
                const auto & event = epair.second;
                if ((event.device_uid != 0) && (devices.count(event.device_uid) == 0)) {
                    LOG_ERROR("Invalid counter directory, event '%s'.'%s' (0x%04x) references invalid device 0x%04x",
                              cat.name.c_str(),
                              event.name.c_str(),
                              event.uid,
                              event.device_uid);
                    return false;
                }
                if ((event.counter_set_uid != 0) && (counterSets.count(event.counter_set_uid) == 0)) {
                    LOG_ERROR(
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
                        LOG_ERROR("Invalid counter directory, event '%s'.'%s' (0x%04x) overlaps another event with "
                                  "the same UID",
                                  cat.name.c_str(),
                                  event.name.c_str(),
                                  event.uid);
                        return false;
                    }
                }

                // map to globalId
                armnn::EventId globalId = makeEventId(devices, counterSets, cat, event);

                // update global id -> cat/event map
                if (!newGlobalIdToCategoryAndEvent.emplace(std::move(globalId), CategoryIndexEventUID {i, event.uid})
                         .second) {
                    LOG_ERROR("Invalid counter directory, event '%s'.'%s' (0x%04x) overlaps another event with the "
                              "same global id",
                              cat.name.c_str(),
                              event.name.c_str(),
                              event.uid);
                    return false;
                }
            }
        }

        updateGlobalWithAvailableEvents(newGlobalIdToCategoryAndEvent, categories, devices, counterSets);

        std::lock_guard<std::mutex> lock {mutex};

        availableCounterDirectoryDevices = devices;
        availableCounterDirectoryCounterSets = counterSets;
        availableCounterDirectoryCategories = categories;
        globalIdToCategoryAndEvent = newGlobalIdToCategoryAndEvent;

        if (captureIsActive) {
            // Send request to ArmNN to update active events
            return sendCounterSelection();
        }
        return true;
    }

    bool SessionStateTracker::onPeriodicCounterSelection(std::uint32_t /*period*/, std::set<std::uint16_t> uids)
    {
        // lock modification of state
        std::lock_guard<std::mutex> lock {mutex};

        // update list
        activeEventUIDs = std::move(uids);

        return true;
    }

    bool SessionStateTracker::onPerJobCounterSelection(std::uint64_t /* objectId */, std::set<std::uint16_t> uids)
    {
        // lock modification of state
        std::lock_guard<std::mutex> lock {mutex};

        // ignore the job information for now

        // update list
        activeEventUIDs = std::move(uids);

        return true;
    }

    bool SessionStateTracker::onPeriodicCounterCapture(std::uint64_t timestamp,
                                                       std::map<std::uint16_t, std::uint32_t> counterIndexValues)
    {
        std::lock_guard<std::mutex> lock {mutex};
        for (const auto & uidAndValue : counterIndexValues) {
            auto match = requestedEventUIDs.find(uidAndValue.first);
            if (match != requestedEventUIDs.end()) {
                if (!counterConsumer.consumeCounterValue(timestamp, match->second, uidAndValue.second)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool SessionStateTracker::onPerJobCounterCapture(bool /* isPre */,
                                                     std::uint64_t timestamp,
                                                     std::uint64_t /* objectRef */,
                                                     std::map<std::uint16_t, std::uint32_t> counterIndexValues)
    {
        // ignore the job information for now

        return onPeriodicCounterCapture(timestamp, counterIndexValues);
    }

    bool SessionStateTracker::forwardPacket(lib::Span<const std::uint8_t> packet)
    {
        return counterConsumer.consumePacket(sessionID, packet);
    }

    EventUIDKeyAndCoreMap SessionStateTracker::formRequestedUIDs(
        const EventKeyMap & eventIdsToKey,
        const std::map<armnn::EventId, CategoryIndexEventUID> & eventIdToCategoryAndEvent,
        const std::vector<CategoryRecord> & availableCategories)
    {
        std::map<std::uint16_t, ApcCounterKeyAndCoreNumber> newRequestedEventUIDs;
        std::set<std::uint16_t> newActiveEventUIDs;

        for (const auto & pair : eventIdsToKey) {
            const armnn::EventId & globalId = pair.first;
            const int key = pair.second;

            const auto it = eventIdToCategoryAndEvent.find(globalId);
            if (it == eventIdToCategoryAndEvent.end()) {
                continue;
            }

            // find category and event
            const auto & category = availableCategories.at(it->second.index);
            const auto & event = category.events_by_uid.at(it->second.uid);

            // add to requested map
            insertRequested(newRequestedEventUIDs, key, event);
        }
        return newRequestedEventUIDs;
    }

    bool SessionStateTracker::doEnableCapture()
    {
        std::lock_guard<std::mutex> lock {mutex};

        captureIsActive = true;

        if (!counterConsumer.consumePacket(sessionID, streamMetadata)) {
            LOG_ERROR("Failed to send Arm NN stream metadata");
            return false;
        }

        // Activate the timeline reporting
        bool requestedTimeline = sendQueue->requestActivateTimelineReporting();
        // Send request to ArmNN to update active events
        bool counterSelectionSent = sendCounterSelection();
        return requestedTimeline && counterSelectionSent;
    }

    bool SessionStateTracker::sendCounterSelection()
    {
        const CaptureMode captureMode = globalState.getCaptureMode();
        const std::uint32_t samplePeriod = globalState.getSamplePeriod();

        requestedEventUIDs = formRequestedUIDs(globalState.getRequestedCounters(),
                                               globalIdToCategoryAndEvent,
                                               availableCounterDirectoryCategories);

        std::set<std::uint16_t> newActiveEventUIDs {keysOf(requestedEventUIDs)};

        // Send request to ArmNN to update active events
        return sendQueue->requestActivateCounterSelection(captureMode, samplePeriod, newActiveEventUIDs);
    }

    bool SessionStateTracker::doDisableCapture()
    {
        // lock modification of state
        std::lock_guard<std::mutex> lock {mutex};

        captureIsActive = false;

        requestedEventUIDs.clear();

        bool requestedTimelineDeactivate = sendQueue->requestDeactivateTimelineReporting();
        bool disablePacketSent = sendQueue->requestDisableCounterSelection();
        return requestedTimelineDeactivate && disablePacketSent;
    }

    void SessionStateTracker::updateGlobalWithAvailableEvents(
        const std::map<EventId, CategoryIndexEventUID> & newGlobalIdToCategoryAndEvent,
        const std::vector<CategoryRecord> & categories,
        const std::map<std::uint16_t, DeviceRecord> & devicesById,
        const std::map<std::uint16_t, CounterSetRecord> & counterSetsById)
    {
        std::vector<std::tuple<armnn::EventId, armnn::EventProperties>> data {};
        for (const auto & i : newGlobalIdToCategoryAndEvent) {
            const auto & cr = categories.at(i.second.index);
            const auto & event = cr.events_by_uid.at(i.second.uid);
            std::optional<std::string> deviceOpt;
            std::optional<std::string> counterSetOpt;
            std::optional<std::uint16_t> counterSetCount;
            if (event.device_uid > 0) {
                deviceOpt = devicesById.at(event.device_uid).name;
            }
            if (event.counter_set_uid > 0) {
                const auto & csrecord = counterSetsById.at(event.counter_set_uid);
                counterSetOpt = csrecord.name;
                counterSetCount = csrecord.count;
            }

            armnn::EventId eventId {cr.name, deviceOpt, counterSetOpt, event.name};
            armnn::EventProperties eventProperties {(counterSetCount ? *counterSetCount : std::uint16_t {0}),
                                                    event.clazz,
                                                    event.interpolation,
                                                    event.multiplier,
                                                    event.description,
                                                    event.units};
            std::tuple<armnn::EventId, armnn::EventProperties> tuple {std::move(eventId), std::move(eventProperties)};

            data.emplace_back(std::move(tuple));
        }

        globalState.addEvents(std::move(data));
    }
}
