/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_ARMNN_SESSION_STATE_TRACKER_H
#define INCLUDE_ARMNN_SESSION_STATE_TRACKER_H

#include "armnn/IPacketConsumer.h"
#include "armnn/IGlobalState.h"
#include "armnn/ICounterDirectoryConsumer.h"
#include "armnn/IPeriodicCounterSelectionConsumer.h"
#include "armnn/ISessionPacketSender.h"

#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace armnn {
    /** Tuple of (key, core) where 'key' is the APC counter key identifier, core is the core number associated with the counter */
    struct ApcCounterKeyAndCoreNumber {
        int key;
        unsigned core;

        inline bool operator==(ApcCounterKeyAndCoreNumber that) const
        {
            return (key == that.key) && (core == that.core);
        }
    };

    class IGlobalCounterConsumer {
    public:
        virtual ~IGlobalCounterConsumer() = default;

        virtual bool consumerCounterValue(std::uint64_t timestamp,
                                          ApcCounterKeyAndCoreNumber keyAndCore,
                                          std::uint32_t counterValue) = 0;
    };

    /**
     * This class manages the state for each connected Session
     */
    class SessionStateTracker : public IPacketConsumer {
    public:
        SessionStateTracker(IGlobalState & globalState,
                            IGlobalCounterConsumer & globalCounterConsumer,
                            ISessionPacketSender & sendQueue);

        virtual ~SessionStateTracker();
        SessionStateTracker(const SessionStateTracker &) = delete;
        SessionStateTracker & operator=(const SessionStateTracker & rhs) = delete;
        SessionStateTracker(SessionStateTracker &&) = delete;
        SessionStateTracker & operator=(SessionStateTracker && rhs) = delete;

        // see ICounterDirectoryConsumer
        virtual bool onCounterDirectory(std::map<std::uint16_t, DeviceRecord> devices,
                                        std::map<std::uint16_t, CounterSetRecord> counterSets,
                                        std::vector<CategoryRecord> categories) override;
        // see IPeriodicCounterSelectionConsumer
        virtual bool onPeriodicCounterSelection(std::uint32_t period, std::set<std::uint16_t> uids) override;
        // see IPerJobCounterSelectionConsumer
        virtual bool onPerJobCounterSelection(std::uint64_t objectId, std::set<std::uint16_t> uids) override;
        //see IPeriodicCounterCaptureConsumer
        virtual bool onPeriodicCounterCapture(std::uint64_t timestamp,
                                              std::map<std::uint16_t, std::uint32_t> counterIndexValues) override;
        //see IPerJobCounterCaptureConsumer
        virtual bool onPerJobCounterCapture(bool isPre,
                                            std::uint64_t timestamp,
                                            std::uint64_t objectRef,
                                            std::map<std::uint16_t, std::uint32_t> counterIndexValues) override;
        /**
         * Request that the provided events are enabled.
         *
         * The map's key is the global id string of the event.
         * The mapped value is the APC key used to identify that counter
         *
         * @param eventIdAndKey
         * @return True on success, false on error
         */
        bool doRequestEnableEvents(const std::map<EventId, int> & eventIdAndKey);

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

        /**
         * Utility method to handle update to requested/active event set
         *
         * @param newRequestedEventUIDs
         * @param newActiveEventUIDs
         * @param captureIsActive
         * @param key
         * @param cat
         * @param event
         * @return True if updated correctly, false on error
         */
        static bool insertRequested(std::map<std::uint16_t, ApcCounterKeyAndCoreNumber> & newRequestedEventUIDs,
                                    std::set<std::uint16_t> & newActiveEventUIDs,
                                    bool captureIsActive,
                                    int key,
                                    const CategoryRecord & cat,
                                    const EventRecord & event);

        static EventId makeEventId(
            const std::map<std::uint16_t, ICounterDirectoryConsumer::DeviceRecord> & deviceMap,
            const std::map<std::uint16_t, ICounterDirectoryConsumer::CounterSetRecord> & counterSetMap,
            const ICounterDirectoryConsumer::CategoryRecord & category,
            const ICounterDirectoryConsumer::EventRecord & record);

        void updateGlobalWithNewCategories(
            const std::map<EventId, CategoryIndexEventUID> & newGlobalIdToCategoryAndEvent,
            const std::vector<CategoryRecord> & categories,
            const std::map<std::uint16_t, DeviceRecord> & devicesById,
            const std::map<std::uint16_t, CounterSetRecord> & counterSetsById);

        // global state object
        IGlobalState & globalState;

        IGlobalCounterConsumer & globalCounterConsumer;

        // The sender for commands to target
        ISessionPacketSender & sendQueue;

        // mutex to protect access/modification of maps
        std::mutex mutex;

        // stores the currently available items from the counter directory
        std::map<std::uint16_t, DeviceRecord> availableCounterDirectoryDevices;
        std::map<std::uint16_t, CounterSetRecord> availableCounterDirectoryCounterSets;
        std::vector<CategoryRecord> availableCounterDirectoryCategories;

        // stores EventId structs -> (Category, Event UID) lookups
        std::map<armnn::EventId, CategoryIndexEventUID> globalIdToCategoryAndEvent;

        // requested event UIDs and the APC key + core they map to
        std::map<std::uint16_t, ApcCounterKeyAndCoreNumber> requestedEventUIDs;

        // active event UIDs
        std::set<std::uint16_t> activeEventUIDs;
    };
}

#endif // INCLUDE_ARMNN_SESSION_STATE_TRACKER_H
