/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#include "armnn/CounterDirectoryDecoder.h"

#include "Logging.h"

#include <cstring>
#include <vector>

namespace armnn {
    namespace {
        static_assert(sizeof(std::uint32_t) == 4, "Expected uint32_t to be 4 bytes");

        constexpr std::size_t BODY_HEADER_SIZE = 6 * sizeof(std::uint32_t);
        constexpr std::size_t DEVICE_RECORD_SIZE = 2 * sizeof(std::uint32_t);
        constexpr std::size_t COUNTER_SET_RECORD_SIZE = 2 * sizeof(std::uint32_t);
        constexpr std::size_t CATEGORY_RECORD_SIZE = 3 * sizeof(std::uint32_t);
        constexpr std::size_t EVENT_RECORD_SIZE = 8 * sizeof(std::uint32_t);
        constexpr std::size_t OFFSET_SIZE = sizeof(std::uint32_t);

        struct BodyHeader {
            std::uint16_t device_records_count;
            std::uint16_t counter_set_count;
            std::uint16_t categories_count;
            std::uint32_t device_records_pointer_table_offset;
            std::uint32_t counter_set_pointer_table_offset;
            std::uint32_t categories_pointer_table_offset;
        };

        using Bytes = CounterDirectoryDecoder::Bytes;

        /**
         * Decode a string from the packet
         */
        bool decodeString(ByteOrder byteOrder, const Bytes & bytes, std::uint32_t offset, std::string & str)
        {
            if ((offset + sizeof(std::uint32_t)) > bytes.length) {
                logg.logError("Failed to decode packet, invalid string offset 0x%x", offset);
                return false;
            }

            const std::uint32_t length = byte_order::get_32(byteOrder, bytes, offset);

            if ((offset + sizeof(std::uint32_t) + length) > bytes.length) {
                logg.logError("Failed to decode packet, invalid string length %u at 0x%x", length, offset);
                return false;
            }
            else if (length == 0) {
                str.clear();
            }
            else if (bytes[offset + sizeof(std::uint32_t) + length - 1] == '\0') {
                str = std::string(reinterpret_cast<const char *>(&bytes[offset + sizeof(std::uint32_t)]), length - 1);
            }
            else {
                str = std::string(reinterpret_cast<const char *>(&bytes[offset + sizeof(std::uint32_t)]), length);
            }

            return true;
        }

        /**
         * Decode a device record, add it to the map, by UID
         */
        bool decodeDeviceRecord(ByteOrder byteOrder,
                                Bytes bytes,
                                std::uint32_t offset,
                                std::map<std::uint16_t, ICounterDirectoryConsumer::DeviceRecord> & map)
        {
            if ((offset + DEVICE_RECORD_SIZE) > bytes.length) {
                logg.logError("Failed to decode packet, invalid device record offset 0x%x", offset);
                return false;
            }

            const auto deviceRecord = bytes.subspan(offset);

            const std::uint32_t cores_and_uid = byte_order::get_32(byteOrder, deviceRecord, 0 * sizeof(std::uint32_t));
            const std::uint32_t name_offset = byte_order::get_32(byteOrder, deviceRecord, 1 * sizeof(std::uint32_t));

            std::string name;
            if (!decodeString(byteOrder, deviceRecord, name_offset, name)) {
                logg.logError("Failed to decode packet, could not decode device_record@%x.name offset 0x%x",
                              offset,
                              name_offset);
                return false;
            }

            const std::uint16_t uid = ((cores_and_uid >> 16) & 0xffff);
            const std::uint16_t cores = (cores_and_uid & 0xffff);

            auto pair = map.emplace(uid, ICounterDirectoryConsumer::DeviceRecord {uid, cores, std::move(name)});
            return pair.second;
        }

        /**
         * Decode a counter set record, add it to the map, by UID
         */
        bool decodeCounterSetRecord(ByteOrder byteOrder,
                                    Bytes bytes,
                                    std::uint32_t offset,
                                    std::map<std::uint16_t, ICounterDirectoryConsumer::CounterSetRecord> & map)
        {
            if ((offset + COUNTER_SET_RECORD_SIZE) > bytes.length) {
                logg.logError("Failed to decode packet, invalid counter set record offset 0x%x", offset);
                return false;
            }

            const auto counterSet = bytes.subspan(offset);

            const std::uint32_t count_and_uid = byte_order::get_32(byteOrder, counterSet, 0 * sizeof(std::uint32_t));
            const std::uint32_t name_offset = byte_order::get_32(byteOrder, counterSet, 1 * sizeof(std::uint32_t));

            std::string name;
            if (!decodeString(byteOrder, counterSet, name_offset, name)) {
                logg.logError("Failed to decode packet, could not decode counter_set_record@%x.name offset 0x%x",
                              offset,
                              name_offset);
                return false;
            }

            const std::uint16_t uid = ((count_and_uid >> 16) & 0xffff);
            const std::uint16_t count = (count_and_uid & 0xffff);

            auto pair = map.emplace(uid, ICounterDirectoryConsumer::CounterSetRecord {uid, count, std::move(name)});
            return pair.second;
        }

        /** Convert the bits from u64 into a double */
        double bits_to_double(std::uint64_t bits)
        {
            static_assert(sizeof(bits) == sizeof(double), "Double != 8 bytes");

            double result;
            std::memcpy(&result, &bits, sizeof(double));
            return result;
        }

        /**
         * Decode an event record, add it to the map, by UID
         */
        bool decodeEventRecord(ByteOrder byteOrder,
                               Bytes bytes,
                               std::uint32_t offset,
                               std::map<std::uint16_t, ICounterDirectoryConsumer::EventRecord> & map)
        {
            if ((offset + EVENT_RECORD_SIZE) > bytes.length) {
                logg.logError("Failed to decode packet, invalid event record offset 0x%x", offset);
                return false;
            }

            const auto eventRecord = bytes.subspan(offset);

            const std::uint32_t counter_uid_and_max_counter_uid =
                byte_order::get_32(byteOrder, bytes, (0 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t counter_set_and_device =
                byte_order::get_32(byteOrder, bytes, (1 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t interpolation_and_class =
                byte_order::get_32(byteOrder, bytes, (2 * sizeof(std::uint32_t)) + offset);
            const std::uint64_t multiplier_bits =
                byte_order::get_64(byteOrder, bytes, (3 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t name_offset =
                byte_order::get_32(byteOrder, bytes, (5 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t description_offset =
                byte_order::get_32(byteOrder, bytes, (6 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t units_offset =
                byte_order::get_32(byteOrder, bytes, (7 * sizeof(std::uint32_t)) + offset);

            std::string name;
            if (!decodeString(byteOrder, eventRecord, name_offset, name)) {
                logg.logError("Failed to decode packet, could not decode event_record@%x.name offset 0x%x",
                              offset,
                              name_offset);
                return false;
            }

            std::string description;
            if (!decodeString(byteOrder, eventRecord, description_offset, description)) {
                logg.logError("Failed to decode packet, could not decode event_record@%x.description offset 0x%x",
                              offset,
                              description_offset);
                return false;
            }

            std::string units;
            if ((units_offset != 0) && !decodeString(byteOrder, eventRecord, units_offset, units)) {
                logg.logError("Failed to decode packet, could not decode event_record@%x.units offset 0x%x",
                              offset,
                              units_offset);
                return false;
            }

            const std::uint16_t max_uid = ((counter_uid_and_max_counter_uid >> 16) & 0xffff);
            const std::uint16_t uid = (counter_uid_and_max_counter_uid & 0xffff);
            const std::uint16_t device_uid = ((counter_set_and_device >> 16) & 0xffff);
            const std::uint16_t counter_set_uid = (counter_set_and_device & 0xffff);
            const std::uint16_t clazz = ((interpolation_and_class >> 16) & 0xffff);
            const std::uint16_t interpolation = (interpolation_and_class & 0xffff);
            const double multiplier = bits_to_double(multiplier_bits);

            auto pair = map.emplace(
                uid,
                ICounterDirectoryConsumer::EventRecord {uid,
                                                        max_uid,
                                                        device_uid,
                                                        counter_set_uid,
                                                        ICounterDirectoryConsumer::Class(clazz),
                                                        ICounterDirectoryConsumer::Interpolation(interpolation),
                                                        multiplier,
                                                        std::move(name),
                                                        std::move(description),
                                                        std::move(units)});
            return pair.second;
        }

        /**
         * Decode a category record, add it to the map, by UID
         */
        bool decodeCategoryRecord(ByteOrder byteOrder,
                                  const Bytes & bytes,
                                  std::uint32_t offset,
                                  ICounterDirectoryConsumer::CategoryRecord & record)
        {
            if ((offset + CATEGORY_RECORD_SIZE) > bytes.length) {
                logg.logError("Failed to decode packet, invalid category record offset 0x%x", offset);
                return false;
            }

            const auto category = bytes.subspan(offset);

            const std::uint32_t reserved_and_event_count =
                byte_order::get_32(byteOrder, category, 0 * sizeof(std::uint32_t));
            const std::uint32_t event_pointer_table_offset =
                byte_order::get_32(byteOrder, category, 1 * sizeof(std::uint32_t));
            const std::uint32_t name_offset = byte_order::get_32(byteOrder, category, 2 * sizeof(std::uint32_t));

            // decode name
            if (!decodeString(byteOrder, category, name_offset, record.name)) {
                logg.logError("Failed to decode packet, could not decode category_record@%x.name offset 0x%x",
                              offset,
                              name_offset);
                return false;
            }

            // decode event records
            const std::uint16_t event_count = ((reserved_and_event_count >> 16) & 0xffff);

            record.events_by_uid.clear();

            if (event_count == 0) {
                return true;
            }
            else if ((event_pointer_table_offset + (event_count * sizeof(std::uint32_t))) > category.length) {
                logg.logError(
                    "Failed to decode packet, could not decode event_record_table in category record at offset 0x%x",
                    offset);
                return false;
            }

            const auto events = category.subspan(event_pointer_table_offset);

            for (std::uint32_t i = 0; i < event_count; ++i) {
                const std::uint32_t event_offset = byte_order::get_32(byteOrder, events, i * OFFSET_SIZE);

                if (!decodeEventRecord(byteOrder, events, event_offset, record.events_by_uid)) {
                    logg.logError("Failed to decode packet, could not decode event_record[%u]@%x in category record at "
                                  "offset 0x%x",
                                  i,
                                  event_offset,
                                  offset);
                    return false;
                }
            }

            return true;
        }
    }

    bool CounterDirectoryDecoder::decode(Bytes bytes) const
    {
        // body_header section must exist
        if (bytes.length < BODY_HEADER_SIZE) {
            logg.logError("Failed to decode packet, too short (%zu)", bytes.length);
            return false;
        }

        // read body header
        const std::uint32_t reserved_and_device_records_count =
            byte_order::get_32(byteOrder, bytes, 0 * sizeof(std::uint32_t));
        const std::uint32_t device_records_pointer_table_offset =
            byte_order::get_32(byteOrder, bytes, 1 * sizeof(std::uint32_t));
        const std::uint32_t reserved_and_counter_set_count =
            byte_order::get_32(byteOrder, bytes, 2 * sizeof(std::uint32_t));
        const std::uint32_t counter_set_pointer_table_offset =
            byte_order::get_32(byteOrder, bytes, 3 * sizeof(std::uint32_t));
        const std::uint32_t reserved_and_categories_count =
            byte_order::get_32(byteOrder, bytes, 4 * sizeof(std::uint32_t));
        const std::uint32_t categories_pointer_table_offset =
            byte_order::get_32(byteOrder, bytes, 5 * sizeof(std::uint32_t));

        const auto device_records_count = (reserved_and_device_records_count >> 16) & 0xffff;
        const auto counter_set_count = (reserved_and_counter_set_count >> 16) & 0xffff;
        const auto categories_count = (reserved_and_categories_count >> 16) & 0xffff;

        // validate counts
        if (bytes.size() < device_records_pointer_table_offset + device_records_count * OFFSET_SIZE) {
            logg.logError(
                "Failed to decode packet, device_records_pointer_table_offset/count out of bounds (0x%x:0x%x)",
                device_records_pointer_table_offset,
                device_records_count);
            return false;
        }

        if (bytes.size() < counter_set_pointer_table_offset + counter_set_count * OFFSET_SIZE) {
            logg.logError("Failed to decode packet, counter_set_pointer_table_offset/count out of bounds (0x%x:0x%x)",
                          counter_set_pointer_table_offset,
                          counter_set_count);
            return false;
        }

        if (bytes.size() < categories_pointer_table_offset + categories_count * OFFSET_SIZE) {
            logg.logError("Failed to decode packet, categories_pointer_table_offset/count out of bounds (0x%x:0x%x)",
                          categories_pointer_table_offset,
                          categories_count);
            return false;
        }

        // read the device_record_offsets
        const auto deviceRecords = bytes.subspan(device_records_pointer_table_offset);
        std::map<std::uint16_t, ICounterDirectoryConsumer::DeviceRecord> device_record_map;
        for (std::uint32_t i = 0; i < device_records_count; ++i) {
            const std::uint32_t offset = byte_order::get_32(byteOrder, deviceRecords, i * OFFSET_SIZE);

            if (!decodeDeviceRecord(byteOrder, deviceRecords, offset, device_record_map)) {
                logg.logError("Failed to decode packet, failed to decode device record[%u]@%x", i, offset);
                return false;
            }
        }

        // read the counter_set_offsets
        const auto counterSets = bytes.subspan(counter_set_pointer_table_offset);
        std::map<std::uint16_t, ICounterDirectoryConsumer::CounterSetRecord> counter_set_map;
        for (std::uint32_t i = 0; i < counter_set_count; ++i) {
            const std::uint32_t offset = byte_order::get_32(byteOrder, counterSets, i * OFFSET_SIZE);

            if (!decodeCounterSetRecord(byteOrder, counterSets, offset, counter_set_map)) {
                logg.logError("Failed to decode packet, failed to decode counter set record[%u]@%x", i, offset);
                return false;
            }
        }

        // read the categories_offsets
        const auto categories = bytes.subspan(categories_pointer_table_offset);
        std::vector<ICounterDirectoryConsumer::CategoryRecord> categories_list {categories_count};
        for (std::uint32_t i = 0; i < categories_count; ++i) {
            const std::uint32_t offset = byte_order::get_32(byteOrder, categories, i * OFFSET_SIZE);

            if (!decodeCategoryRecord(byteOrder, categories, offset, categories_list[i])) {
                logg.logError("Failed to decode packet, failed to decode category record[%u]@%x", i, offset);
                return false;
            }
        }

        // pass to consumer
        if (!consumer.onCounterDirectory(std::move(device_record_map),
                                         std::move(counter_set_map),
                                         std::move(categories_list))) {
            logg.logError("Packet consumer returned error ");
            return false;
        }
        return true;
    }
}
