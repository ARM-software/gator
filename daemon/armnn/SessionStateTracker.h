/* Copyright (C) 2019-2021 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_ARMNN_SESSION_STATE_TRACKER_H
#define INCLUDE_ARMNN_SESSION_STATE_TRACKER_H

#include "armnn/ICounterConsumer.h"
#include "armnn/ICounterDirectoryConsumer.h"
#include "armnn/IGlobalState.h"
#include "armnn/IPacketConsumer.h"
#include "armnn/IPeriodicCounterSelectionConsumer.h"
#include "armnn/ISessionPacketSender.h"

#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <vector>

namespace armnn {
    using EventUIDKeyAndCoreMap = std::map<std::uint16_t, ApcCounterKeyAndCoreNumber>;

    /**
     * This class manages the state for each connected Session
     */
    class SessionStateTracker : public IPacketConsumer {
    public:
        SessionStateTracker(IGlobalState & globalState,
                            ICounterConsumer & counterConsumer,
                            std::unique_ptr<ISessionPacketSender> sendQueue,
                            std::uint32_t sessionID,
                            std::vector<std::uint8_t> streamMetadata);

        // see ICounterDirectoryConsumer
        bool onCounterDirectory(std::map<std::uint16_t, DeviceRecord> devices,
                                std::map<std::uint16_t, CounterSetRecord> counterSets,
                                std::vector<CategoryRecord> categories) override;
        // see IPeriodicCounterSelectionConsumer
        bool onPeriodicCounterSelection(std::uint32_t period, std::set<std::uint16_t> uids) override;
        // see IPerJobCounterSelectionConsumer
        bool onPerJobCounterSelection(std::uint64_t objectId, std::set<std::uint16_t> uids) override;
        // see IPeriodicCounterCaptureConsumer
        bool onPeriodicCounterCapture(std::uint64_t timestamp,
                                      std::map<std::uint16_t, std::uint32_t> counterIndexValues) override;
        // see IPerJobCounterCaptureConsumer
        bool onPerJobCounterCapture(bool isPre,
                                    std::uint64_t timestamp,
                                    std::uint64_t objectRef,
                                    std::map<std::uint16_t, std::uint32_t> counterIndexValues) override;

        /**
         * Consumes a raw packet sent from target
         *
         * @returns true if the packet was successfully consumed, false otherwise.
         **/
        bool forwardPacket(lib::Span<const std::uint8_t> packet);

        /** Start capturing data */
        bool doEnableCapture();

        /** Stop capturing data */
        bool doDisableCapture();

        /** @return The set of active counters */
        const std::set<std::uint16_t> & getActiveCounterUIDs() const { return activeEventUIDs; }

    private:
        /**
         * Tuple of (index, uid) where index is the position in availableCounterDirectoryCategories
         * and uid is the uid of the event within that category
         */
        struct CategoryIndexEventUID {
            std::size_t index;
            std::uint16_t uid;
        };

        bool sendCounterSelection();

        void updateGlobalWithAvailableEvents(
            const std::map<EventId, CategoryIndexEventUID> & newGlobalIdToCategoryAndEvent,
            const std::vector<CategoryRecord> & categories,
            const std::map<std::uint16_t, DeviceRecord> & devicesById,
            const std::map<std::uint16_t, CounterSetRecord> & counterSetsById);

        static EventUIDKeyAndCoreMap formRequestedUIDs(
            const EventKeyMap & eventIdsToKey,
            const std::map<armnn::EventId, CategoryIndexEventUID> & eventIdToCategoryAndEvent,
            const std::vector<CategoryRecord> & availableCategories);

        // global state object
        IGlobalState & globalState;

        ICounterConsumer & counterConsumer;

        // The sender for commands to target
        std::unique_ptr<ISessionPacketSender> sendQueue;

        // the raw metadata blob
        std::vector<std::uint8_t> streamMetadata;

        // mutex to protect access/modification of maps
        std::mutex mutex {};

        // stores the currently available items from the counter directory
        std::map<std::uint16_t, DeviceRecord> availableCounterDirectoryDevices {};
        std::map<std::uint16_t, CounterSetRecord> availableCounterDirectoryCounterSets {};
        std::vector<CategoryRecord> availableCounterDirectoryCategories {};

        // stores EventId structs -> (Category, Event UID) lookups
        std::map<armnn::EventId, CategoryIndexEventUID> globalIdToCategoryAndEvent {};

        // requested event UIDs and the APC key + core they map to
        std::map<std::uint16_t, ApcCounterKeyAndCoreNumber> requestedEventUIDs {};

        // active event UIDs
        std::set<std::uint16_t> activeEventUIDs {};

        // the current session
        const std::uint32_t sessionID;

        bool captureIsActive {false};
    };
}

#endif // INCLUDE_ARMNN_SESSION_STATE_TRACKER_H
