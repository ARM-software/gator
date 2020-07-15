/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#include "PacketEncoder.h"

#include "../Logging.h"
#include "ByteOrder.h"
#include "PacketUtility.h"
#include "lib/EnumUtils.h"

#include <algorithm>
#include <cassert>

namespace armnn {

    PacketEncoder::PacketEncoder(ByteOrder byteOrder_) : byteOrder(byteOrder_) {}

    template<typename T>
    void appendBytes(std::vector<std::uint8_t> & input, T value, ByteOrder byteOrder)
    {
        const std::uint8_t * bytePointer = reinterpret_cast<std::uint8_t *>(&value);
        if (byteOrder != byte_order::SYSTEM) {
            std::reverse_copy(bytePointer, bytePointer + sizeof(T), std::back_inserter(input));
        }
        else {
            std::copy(bytePointer, bytePointer + sizeof(T), std::back_inserter(input));
        }
    }

    void PacketEncoder::appendHeader(const std::uint32_t packetIdentifier,
                                     const std::uint32_t dataLength,
                                     std::vector<std::uint8_t> & payload)
    {
        appendBytes(payload, packetIdentifier, byteOrder);
        appendBytes(payload, dataLength, byteOrder);
    }

    std::vector<std::uint8_t> PacketEncoder::encodePeriodicCounterSelectionRequest(
        std::uint32_t period,
        const std::set<std::uint16_t> & eventUids)
    {
        const std::uint32_t packetIdentifier = lib::toEnumValue(PacketType::PeriodicCounterSelectionPkt);
        const std::uint32_t dataLength =
            eventUids.empty() ? 0 : (sizeof(period) + (eventUids.size() * sizeof(std::uint16_t)));
        std::vector<std::uint8_t> payload;
        appendHeader(packetIdentifier, dataLength, payload);

        if (eventUids.empty()) {
            logg.logMessage("Event uids are empty, creating disable counter collection packet data");
        }
        else {
            assert(dataLength > 0 && "PeriodicCounterSelection packet datalength is zero but eventUids not empty");
            appendBytes(payload, period, byteOrder);
            for (auto eventUid : eventUids) {
                appendBytes(payload, eventUid, byteOrder);
            }
        }

        return payload;
    }

    std::vector<std::uint8_t> PacketEncoder::encodePerJobCounterSelectionRequest(
        std::uint64_t objectId,
        const std::set<std::uint16_t> & eventUids)
    {
        const std::uint32_t packetIdentifier = lib::toEnumValue(PacketType::PerJobCounterSelectionPkt);
        const std::uint32_t dataLength =
            eventUids.empty() ? 0 : (sizeof(objectId) + (eventUids.size() * sizeof(std::uint16_t)));
        std::vector<std::uint8_t> payload;
        appendHeader(packetIdentifier, dataLength, payload);

        if (eventUids.empty()) {
            logg.logMessage("Event uids are empty, creating disable counter collection packet data");
        }
        else {
            assert(dataLength > 0 && "PerJobCounterSelection packet datalength is zero but eventUids not empty");
            appendBytes(payload, objectId, byteOrder);
            for (auto eventUid : eventUids) {
                appendBytes(payload, eventUid, byteOrder);
            }
        }

        return payload;
    }

    std::vector<std::uint8_t> PacketEncoder::encodeConnectionAcknowledge()
    {
        std::vector<std::uint8_t> payload;
        const auto packetIdentifier = static_cast<uint32_t>(PacketType::ConnectionAckPkt);
        appendHeader(packetIdentifier, 0, payload);
        return payload;
    }

    std::vector<std::uint8_t> PacketEncoder::encodeCounterDirectoryRequest()
    {
        std::vector<std::uint8_t> payload;
        const auto packetIdentifier = static_cast<uint32_t>(PacketType::CounterDirectoryReqPkt);
        appendHeader(packetIdentifier, 0, payload);
        return payload;
    }

    bool PacketEncoder::isValidPacketVersions(const std::vector<PacketVersionTable> & pktVersionTable)
    {
        bool validPacket = false;
        for (auto pktVersion : pktVersionTable) {
            auto packetVersion = pktVersion.packetVersion;
            auto packetType = makePacketType(pktVersion.packetFamily, pktVersion.packetId, 0);
            switch (packetType) {
                case lib::toEnumValue(PacketType::CounterDirectoryReqPkt):
                case lib::toEnumValue(PacketType::ConnectionAckPkt):
                case lib::toEnumValue(PacketType::PerJobCounterSelectionPkt):
                case lib::toEnumValue(PacketType::PeriodicCounterSelectionPkt): //
                {
                    auto majorVersion = getBits(packetVersion, 22, 31);
                    if (majorVersion != SUPPORTED_PACKET_MAJOR_VERSION[0]) {
                        logg.logError(
                            "Unsupported packet version (%u:%u:%u) for packet type (family=0x%02x, id=0x%03x)",
                            majorVersion,
                            getBits(packetVersion, 12, 22),
                            getBits(packetVersion, 0, 11),
                            pktVersion.packetFamily,
                            pktVersion.packetId);
                        return false;
                    }
                    validPacket = true;
                } break;
                default:
                    // We don't care about anything we don't need to encode
                    break;
            }
        }
        return validPacket;
    }

} /* namespace armnn */
