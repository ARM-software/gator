/* Copyright (C) 2019-2023 by Arm Limited. All rights reserved. */

#include "armnn/CounterDirectoryStateUtils.h"

#include "armnn/ICounterDirectoryConsumer.h"
#include "lib/Format.h"

#include <cctype>
#include <cstdint>
#include <locale>
#include <map>
#include <string>

namespace armnn {

    namespace {
        std::string makeId(std::string str)
        {
            const auto & loc = std::locale::classic();
            const auto length = str.length();
            bool onbreak = true;
            std::string::size_type pos = 0;

            for (std::string::size_type i = 0; i < length; ++i) {
                if (std::isalpha(str[i], loc)) {
                    // make camel case
                    if (onbreak) {
                        str[pos] = std::toupper(str[i], loc);
                        onbreak = false;
                    }
                    else {
                        str[pos] = std::tolower(str[i], loc);
                    }
                    pos += 1;
                }
                else if (std::isdigit(str[i], loc)) {
                    // copy digit, but dont change camel case state
                    str[pos] = str[i];
                    pos += 1;
                }
                else {
                    onbreak = true;
                }
            }

            // trim off any remaining bits
            str.resize(pos);

            return str;
        }

        void append(lib::Format & formatter,
                    const std::map<std::uint16_t, ICounterDirectoryConsumer::DeviceRecord> & deviceMap,
                    const std::map<std::uint16_t, ICounterDirectoryConsumer::CounterSetRecord> & counterSetMap,
                    std::uint16_t device_uid,
                    std::uint16_t counter_set_uid,
                    const std::string & name)
        {
            formatter << makeId(name);

            if (device_uid != 0) {
                formatter << "__" << makeGloballyUniqueId(deviceMap.at(device_uid));
            }

            if (counter_set_uid != 0) {
                formatter << "__" << makeGloballyUniqueId(counterSetMap.at(counter_set_uid));
            }
        }
    }

    std::string makeGloballyUniqueId(const ICounterDirectoryConsumer::DeviceRecord & record)
    {
        return (lib::Format() << makeId(record.name) << "_" << record.cores);
    }

    std::string makeGloballyUniqueId(const ICounterDirectoryConsumer::CounterSetRecord & record)
    {
        return (lib::Format() << makeId(record.name) << "_" << record.count);
    }

    std::string makeGloballyEventCategoryUniqueId(
        const std::map<std::uint16_t, ICounterDirectoryConsumer::DeviceRecord> & deviceMap,
        const std::map<std::uint16_t, ICounterDirectoryConsumer::CounterSetRecord> & counterSetMap,
        const ICounterDirectoryConsumer::CategoryRecord & category,
        const ICounterDirectoryConsumer::EventRecord & record)
    {
        lib::Format formatter;

        append(formatter, deviceMap, counterSetMap, record.device_uid, record.counter_set_uid, category.name);

        return formatter;
    }

    std::string makeGloballyUniqueId(
        const std::map<std::uint16_t, ICounterDirectoryConsumer::DeviceRecord> & deviceMap,
        const std::map<std::uint16_t, ICounterDirectoryConsumer::CounterSetRecord> & counterSetMap,
        const ICounterDirectoryConsumer::CategoryRecord & category,
        const ICounterDirectoryConsumer::EventRecord & record)
    {
        lib::Format formatter;

        formatter << makeId(category.name) << "__";

        append(formatter, deviceMap, counterSetMap, record.device_uid, record.counter_set_uid, record.name);

        return formatter;
    }
}
