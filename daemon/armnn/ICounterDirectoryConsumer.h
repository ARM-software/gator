/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */
#ifndef INCLUDE_ARMNN_I_COUNTER_DIRECTORY_CONSUMER_H
#define INCLUDE_ARMNN_I_COUNTER_DIRECTORY_CONSUMER_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace armnn {
    /**
     * Interface for consumer that is called with decoded contents of counter directory packet
     */
    class ICounterDirectoryConsumer {
    public:
        enum class Class : std::uint16_t { DELTA = 0, ABSOLUTE = 1 };

        enum class Interpolation : std::uint16_t { LINEAR = 0, STEP = 1 };

        struct DeviceRecord {
            std::uint16_t uid;
            std::uint16_t cores;
            std::string name;

            DeviceRecord() : uid(0), cores(0), name() {}
            DeviceRecord(std::uint16_t uid, std::uint16_t cores, std::string name)
                : uid(uid), cores(cores), name(std::move(name))
            {
            }
        };

        struct CounterSetRecord {
            std::uint16_t uid;
            std::uint16_t count;
            std::string name;

            CounterSetRecord() : uid(0), count(0), name() {}
            CounterSetRecord(std::uint16_t uid, std::uint16_t count, std::string name)
                : uid(uid), count(count), name(std::move(name))
            {
            }
        };

        struct EventRecord {
            std::uint16_t uid;
            std::uint16_t max_uid;
            std::uint16_t device_uid;
            std::uint16_t counter_set_uid;
            Class clazz;
            Interpolation interpolation;
            double multiplier;
            std::string name;
            std::string description;
            std::string units;

            EventRecord()
                : uid(0),
                  max_uid(0),
                  device_uid(0),
                  counter_set_uid(0),
                  clazz(Class::DELTA),
                  interpolation(Interpolation::LINEAR),
                  multiplier(0),
                  name(),
                  description(),
                  units()
            {
            }

            EventRecord(std::uint16_t uid,
                        std::uint16_t max_uid,
                        std::uint16_t device_uid,
                        std::uint16_t counter_set_uid,
                        Class clazz,
                        Interpolation interpolation,
                        double multiplier,
                        std::string name,
                        std::string description,
                        std::string units)
                : uid(uid),
                  max_uid(max_uid),
                  device_uid(device_uid),
                  counter_set_uid(counter_set_uid),
                  clazz(clazz),
                  interpolation(interpolation),
                  multiplier(multiplier),
                  name(std::move(name)),
                  description(std::move(description)),
                  units(std::move(units))
            {
            }
        };

        struct CategoryRecord {
            std::string name;
            std::map<std::uint16_t, EventRecord> events_by_uid;

            CategoryRecord() : name(), events_by_uid() {}
            CategoryRecord(std::string name, std::map<std::uint16_t, EventRecord> events_by_uid)
                : name(std::move(name)), events_by_uid(std::move(events_by_uid))
            {
            }
        };

        virtual ~ICounterDirectoryConsumer() = default;

        /**
         * Called with the contents parsed from the counter directory packet
         *
         * @param devices The map of devices by UID
         * @param counterSets The map of counter sets by UID
         * @param categories The list of categories
         * @return False if there was some error in the counter directory data
         */
        virtual bool onCounterDirectory(std::map<std::uint16_t, DeviceRecord> devices,
                                        std::map<std::uint16_t, CounterSetRecord> counterSets,
                                        std::vector<CategoryRecord> categories) = 0;
    };
}

#endif // INCLUDE_ARMNN_I_COUNTER_DIRECTORY_CONSUMER_H
