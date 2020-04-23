/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#include "armnn/CounterDirectoryDecoder.h"

#include "Logging.h"

#include <cstring>
#include <vector>

namespace armnn {
    namespace {
        static_assert(sizeof(std::uint32_t) == 4, "Expected uint32_t to be 4 bytes");

        static constexpr std::size_t BODY_HEADER_SIZE = 6 * sizeof(std::uint32_t);
        static constexpr std::size_t DEVICE_RECORD_SIZE = 2 * sizeof(std::uint32_t);
        static constexpr std::size_t COUNTER_SET_RECORD_SIZE = 2 * sizeof(std::uint32_t);
        static constexpr std::size_t CATEGORY_RECORD_SIZE = 4 * sizeof(std::uint32_t);
        static constexpr std::size_t EVENT_RECORD_SIZE = 8 * sizeof(std::uint32_t);

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
        static bool decodeString(ByteOrder byteOrder, const Bytes & bytes, std::uint32_t offset, std::string & str)
        {
            if ((offset + sizeof(std::uint32_t)) > bytes.length) {
                logg.logError("Failed to decode packet, invalid string offset 0x%x", offset);
                return false;
            }

            const std::uint32_t length = byte_order::get_aligned_32(byteOrder, bytes, offset);

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
        static bool decodeDeviceRecord(ByteOrder byteOrder,
                                       const Bytes & bytes,
                                       std::uint32_t offset,
                                       std::map<std::uint16_t, ICounterDirectoryConsumer::DeviceRecord> & map)
        {
            if ((offset + DEVICE_RECORD_SIZE) > bytes.length) {
                logg.logError("Failed to decode packet, invalid device record offset 0x%x", offset);
                return false;
            }

            const std::uint32_t w0 = byte_order::get_aligned_32(byteOrder, bytes, (0 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t w1 = byte_order::get_aligned_32(byteOrder, bytes, (1 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t name_offset = w1 + offset + DEVICE_RECORD_SIZE;

            std::string name;
            if (!decodeString(byteOrder, bytes, name_offset, name)) {
                logg.logError("Failed to decode packet, could not decode device_record@%x.name offset 0x%x",
                              offset,
                              name_offset);
                return false;
            }

            const std::uint16_t uid = ((w0 >> 16) & 0xffff);
            const std::uint16_t cores = (w0 & 0xffff);

            auto pair = map.emplace(uid, ICounterDirectoryConsumer::DeviceRecord{uid, cores, std::move(name)});
            return pair.second;
        }

        /**
         * Decode a counter set record, add it to the map, by UID
         */
        static bool decodeCounterSetRecord(ByteOrder byteOrder,
                                           const Bytes & bytes,
                                           std::uint32_t offset,
                                           std::map<std::uint16_t, ICounterDirectoryConsumer::CounterSetRecord> & map)
        {
            if ((offset + COUNTER_SET_RECORD_SIZE) > bytes.length) {
                logg.logError("Failed to decode packet, invalid counter set record offset 0x%x", offset);
                return false;
            }

            const std::uint32_t w0 = byte_order::get_aligned_32(byteOrder, bytes, (0 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t w1 = byte_order::get_aligned_32(byteOrder, bytes, (1 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t name_offset = w1 + offset + COUNTER_SET_RECORD_SIZE;

            std::string name;
            if (!decodeString(byteOrder, bytes, name_offset, name)) {
                logg.logError("Failed to decode packet, could not decode counter_set_record@%x.name offset 0x%x",
                              offset,
                              name_offset);
                return false;
            }

            const std::uint16_t uid = ((w0 >> 16) & 0xffff);
            const std::uint16_t count = (w0 & 0xffff);

            auto pair = map.emplace(uid, ICounterDirectoryConsumer::CounterSetRecord{uid, count, std::move(name)});
            return pair.second;
        }

        /** Convert the bits from a pair of u32's into a double */
        static double bits_to_double(std::uint32_t l, std::uint32_t h)
        {
            struct {
                std::uint32_t l;
                std::uint32_t h;
            } pair{l, h};

            static_assert(sizeof(pair) == sizeof(double), "Double != 8 bytes");

            double result;
            std::memcpy(&result, &pair, sizeof(double));
            return result;
        }

        /**
         * Decode an event record, add it to the map, by UID
         */
        static bool decodeEventRecord(ByteOrder byteOrder,
                                      const Bytes & bytes,
                                      std::uint32_t offset,
                                      std::map<std::uint16_t, ICounterDirectoryConsumer::EventRecord> & map)
        {
            if ((offset + EVENT_RECORD_SIZE) > bytes.length) {
                logg.logError("Failed to decode packet, invalid event record offset 0x%x", offset);
                return false;
            }

            const std::uint32_t w0 = byte_order::get_aligned_32(byteOrder, bytes, (0 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t w1 = byte_order::get_aligned_32(byteOrder, bytes, (1 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t w2 = byte_order::get_aligned_32(byteOrder, bytes, (2 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t w3 = byte_order::get_aligned_32(byteOrder, bytes, (3 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t w4 = byte_order::get_aligned_32(byteOrder, bytes, (4 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t w5 = byte_order::get_aligned_32(byteOrder, bytes, (5 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t w6 = byte_order::get_aligned_32(byteOrder, bytes, (6 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t w7 = byte_order::get_aligned_32(byteOrder, bytes, (7 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t pool_offset = offset + EVENT_RECORD_SIZE;

            std::uint32_t name_offset = pool_offset + w5;
            std::uint32_t description_offset = pool_offset + w6;
            std::uint32_t units_offset = pool_offset + w7;

            std::string name;
            if (!decodeString(byteOrder, bytes, name_offset, name)) {
                logg.logError("Failed to decode packet, could not decode event_record@%x.name offset 0x%x",
                              offset,
                              name_offset);
                return false;
            }

            std::string description;
            if (!decodeString(byteOrder, bytes, description_offset, description)) {
                logg.logError("Failed to decode packet, could not decode event_record@%x.description offset 0x%x",
                              offset,
                              description_offset);
                return false;
            }

            std::string units;
            if ((w7 != 0) && !decodeString(byteOrder, bytes, units_offset, units)) {
                logg.logError("Failed to decode packet, could not decode event_record@%x.units offset 0x%x",
                              offset,
                              units_offset);
                return false;
            }

            const std::uint16_t max_uid = ((w0 >> 16) & 0xffff);
            const std::uint16_t uid = (w0 & 0xffff);
            const std::uint16_t device_uid = ((w1 >> 16) & 0xffff);
            const std::uint16_t counter_set_uid = (w1 & 0xffff);
            const std::uint16_t clazz = ((w2 >> 16) & 0xffff);
            const std::uint16_t interpolation = (w2 & 0xffff);
            const double multiplier = bits_to_double(w3, w4);

            auto pair = map.emplace(
                uid,
                ICounterDirectoryConsumer::EventRecord{uid,
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
        static bool decodeCategoryRecord(ByteOrder byteOrder,
                                         const Bytes & bytes,
                                         std::uint32_t offset,
                                         ICounterDirectoryConsumer::CategoryRecord & record)
        {
            if ((offset + CATEGORY_RECORD_SIZE) > bytes.length) {
                logg.logError("Failed to decode packet, invalid category record offset 0x%x", offset);
                return false;
            }

            const std::uint32_t w0 = byte_order::get_aligned_32(byteOrder, bytes, (0 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t w1 = byte_order::get_aligned_32(byteOrder, bytes, (1 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t w2 = byte_order::get_aligned_32(byteOrder, bytes, (2 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t w3 = byte_order::get_aligned_32(byteOrder, bytes, (3 * sizeof(std::uint32_t)) + offset);
            const std::uint32_t pool_offset = offset + CATEGORY_RECORD_SIZE;

            // reserved word
            if ((w1 & 0xffff) != 0) {
                logg.logError("Failed to decode packet, invalid res0 field in category record at offset 0x%x", offset);
                return false;
            }

            // header fields
            record.device_uid = ((w0 >> 16) & 0xffff);
            record.counter_set_uid = (w0 & 0xffff);

            // decode name
            const std::uint32_t name_offset = pool_offset + w3;
            if (!decodeString(byteOrder, bytes, name_offset, record.name)) {
                logg.logError("Failed to decode packet, could not decode category_record@%x.name offset 0x%x",
                              offset,
                              name_offset);
                return false;
            }

            // decode event records
            const std::uint16_t event_count = ((w1 >> 16) & 0xffff);
            const std::uint32_t event_pointer_table_offset = pool_offset + w2;

            record.events_by_uid.clear();

            if (event_count == 0) {
                return true;
            }
            else if ((event_pointer_table_offset + (event_count * sizeof(std::uint32_t))) > bytes.length) {
                logg.logError(
                    "Failed to decode packet, could not decode event_record_table in category record at offset 0x%x",
                    offset);
                return false;
            }

            for (std::uint32_t i = 0; i < event_count; ++i) {
                const std::uint32_t event_offset =
                    byte_order::get_aligned_32(byteOrder,
                                               bytes,
                                               (i * sizeof(std::uint32_t)) + event_pointer_table_offset);

                if (!decodeEventRecord(byteOrder, bytes, event_offset + pool_offset, record.events_by_uid)) {
                    logg.logError("Failed to decode packet, could not decode event_record[%u]@%x in category record at "
                                  "offset 0x%x",
                                  i,
                                  event_offset + pool_offset,
                                  offset);
                    return false;
                }
            }

            return true;
        }
    }

    bool CounterDirectoryDecoder::decode(Bytes bytes) const
    {
        // it is ok to skip empty packets
        if (bytes.length == 0) {
            return true;
        }

        // body_header section must exist
        if (bytes.length < BODY_HEADER_SIZE) {
            logg.logError("Failed to decode packet, too short (%zu)", bytes.length);
            return false;
        }

        // read body header
        BodyHeader body_header;

        {
            const std::uint32_t w0 = byte_order::get_aligned_32(byteOrder, bytes, 0 * sizeof(std::uint32_t));
            const std::uint32_t w1 = byte_order::get_aligned_32(byteOrder, bytes, 1 * sizeof(std::uint32_t));
            const std::uint32_t w2 = byte_order::get_aligned_32(byteOrder, bytes, 2 * sizeof(std::uint32_t));
            const std::uint32_t w3 = byte_order::get_aligned_32(byteOrder, bytes, 3 * sizeof(std::uint32_t));
            const std::uint32_t w4 = byte_order::get_aligned_32(byteOrder, bytes, 4 * sizeof(std::uint32_t));
            const std::uint32_t w5 = byte_order::get_aligned_32(byteOrder, bytes, 5 * sizeof(std::uint32_t));

            // reserved words must be zero
            if (((w0 & 0xffff) != 0) || ((w2 & 0xffff) != 0) || ((w4 & 0xffff) != 0)) {
                logg.logError("Failed to decode packet, invalid res0 fields in header (%04x:%04x:%04x)",
                              w0 & 0xffff,
                              w2 & 0xffff,
                              w4 & 0xffff);
                return false;
            }

            body_header.device_records_count = (w0 >> 16) & 0xffff;
            body_header.device_records_pointer_table_offset = w1;
            body_header.counter_set_count = (w2 >> 16) & 0xffff;
            body_header.counter_set_pointer_table_offset = w3;
            body_header.categories_count = (w4 >> 16) & 0xffff;
            body_header.categories_pointer_table_offset = w5;

            // validate counts
            if ((body_header.device_records_count > 0) &&
                ((body_header.device_records_pointer_table_offset < BODY_HEADER_SIZE) ||
                 (((body_header.device_records_count * 4) + body_header.device_records_pointer_table_offset) >
                  bytes.length))) {
                logg.logError("Failed to decode packet, device record pointer table out of bounds (%u: %u)",
                              body_header.device_records_count,
                              body_header.device_records_pointer_table_offset);
                return false;
            }

            if ((body_header.counter_set_count > 0) &&
                ((body_header.counter_set_pointer_table_offset < BODY_HEADER_SIZE) ||
                 (((body_header.counter_set_count * 4) + body_header.counter_set_pointer_table_offset) >
                  bytes.length))) {
                logg.logError("Failed to decode packet, counter set record pointer table out of bounds (%u: %u)",
                              body_header.counter_set_count,
                              body_header.counter_set_pointer_table_offset);
                return false;
            }

            if ((body_header.categories_count > 0) &&
                ((body_header.categories_pointer_table_offset < BODY_HEADER_SIZE) ||
                 (((body_header.categories_count * 4) + body_header.categories_pointer_table_offset) > bytes.length))) {
                logg.logError("Failed to decode packet, categories record pointer table out of bounds (%u: %u)",
                              body_header.categories_count,
                              body_header.categories_pointer_table_offset);
                return false;
            }
        }

        // read the device_record_offsets
        std::map<std::uint16_t, ICounterDirectoryConsumer::DeviceRecord> device_record_map;
        for (std::uint32_t i = 0; i < body_header.device_records_count; ++i) {
            const std::uint32_t offset = byte_order::get_aligned_32(
                byteOrder,
                bytes, //
                (i * sizeof(std::uint32_t)) + body_header.device_records_pointer_table_offset);

            if (!decodeDeviceRecord(byteOrder, bytes, offset, device_record_map)) {
                logg.logError("Failed to decode packet, failed to decode device record[%u]@%x", i, offset);
                return false;
            }
        }

        // read the counter_set_offsets
        std::map<std::uint16_t, ICounterDirectoryConsumer::CounterSetRecord> counter_set_map;
        for (std::uint32_t i = 0; i < body_header.counter_set_count; ++i) {
            const std::uint32_t offset =
                byte_order::get_aligned_32(byteOrder,
                                           bytes, //
                                           (i * sizeof(std::uint32_t)) + body_header.counter_set_pointer_table_offset);

            if (!decodeCounterSetRecord(byteOrder, bytes, offset, counter_set_map)) {
                logg.logError("Failed to decode packet, failed to decode counter set record[%u]@%x", i, offset);
                return false;
            }
        }

        // read the categories_offsets
        std::vector<ICounterDirectoryConsumer::CategoryRecord> categories_list{body_header.categories_count};
        for (std::uint32_t i = 0; i < body_header.categories_count; ++i) {
            const std::uint32_t offset =
                byte_order::get_aligned_32(byteOrder,
                                           bytes, //
                                           (i * sizeof(std::uint32_t)) + body_header.categories_pointer_table_offset);

            if (!decodeCategoryRecord(byteOrder, bytes, offset, categories_list[i])) {
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
