/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_ARMNN_COUNTER_DIRECTORY_STATE_UTILS_H
#define INCLUDE_ARMNN_COUNTER_DIRECTORY_STATE_UTILS_H

#include "armnn/ICounterDirectoryConsumer.h"

/*
 * These utility functions create unique identifier strings for different aspects of the counter directory packet.
 * The allow us to reconcile items across different versions of armnn and different processes.
 *
 * Assumptions:
 * - The UID's in the packet may be allocated dynamically.
 * - Even if the UID's are allocated statically, they may vary from version to version.
 * - The names of items will usually remain constant across versions and processes so long as the continue to represent
 *   the same thing.
 * - The name of a device or counter set will be unique for the thing that it represents
 * - Likewise, the tuple of (category name, event name) is unique for each event;
 *   i.e. there will never be more than one counter, representing different data, that has the same tuple.
 */

namespace armnn {
    /**
     * Deterministically create a unique id string for the device record.
     *
     * The string returned is always the same for a given pair of {tolower(record.name), record.cores}, but will be
     * unique for different values of that pair.
     *
     * @param record
     * @return An ID string
     */
    std::string makeGloballyUniqueId(const ICounterDirectoryConsumer::DeviceRecord & record);

    /**
     * Deterministically create a unique id string for the counter set record.
     *
     * The string returned is always the same for a given pair of {tolower(record.name), record.count}, but will be
     * unique for different values of that pair.
     *
     * @param record
     * @return An ID string
     */
    std::string makeGloballyUniqueId(const ICounterDirectoryConsumer::CounterSetRecord & record);

    /**
     * Deterministically create a unique id string for the category an event belongs to.
     *
     * The string returned is always the same for a given tuple of
     * {tolower(category.name), makeGloballyUniqueId(event.device), makeGloballyUniqueId(event.counter_set)},
     * but will be unique for different values of that tuple.
     *
     * Note that the event's device/counter set are used, not the category's since the category only exists to group/name
     * events visually.
     *
     * @param deviceMap
     * @param counterSetMap
     * @param category
     * @param record
     * @return The ID string
     */
    std::string makeGloballyEventCategoryUniqueId(
        const std::map<std::uint16_t, ICounterDirectoryConsumer::DeviceRecord> & deviceMap,
        const std::map<std::uint16_t, ICounterDirectoryConsumer::CounterSetRecord> & counterSetMap,
        const ICounterDirectoryConsumer::CategoryRecord & category,
        const ICounterDirectoryConsumer::EventRecord & record);

    /**
     * Deterministically create a unique id string for an event.
     *
     * The string returned is always the same for a given tuple of
     * {tolower(category.name), tolower(event.name), makeGloballyUniqueId(event.device), makeGloballyUniqueId(event.counter_set)},
     * but will be unique for different values of that tuple.
     *
     * @param deviceMap
     * @param counterSetMap
     * @param category
     * @param record
     * @return The ID string
     */
    std::string makeGloballyUniqueId(
        const std::map<std::uint16_t, ICounterDirectoryConsumer::DeviceRecord> & deviceMap,
        const std::map<std::uint16_t, ICounterDirectoryConsumer::CounterSetRecord> & counterSetMap,
        const ICounterDirectoryConsumer::CategoryRecord & category,
        const ICounterDirectoryConsumer::EventRecord & record);
}

#endif //INCLUDE_ARMNN_COUNTER_DIRECTORY_STATE_UTILS_H
