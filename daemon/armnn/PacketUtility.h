/* Copyright (C) 2020-2021 by Arm Limited. All rights reserved. */

#ifndef ARMNN_PACKETUTILITY_H_
#define ARMNN_PACKETUTILITY_H_

#include "armnn/ByteOrder.h"

#include <cstdint>

namespace armnn {

    using Bytes = lib::Span<const std::uint8_t>;

    static constexpr std::uint32_t makePacketType(std::uint8_t family, std::uint16_t id, std::uint16_t extra = 0)
    {
        return ((std::uint32_t(family & 0x3F) << 26) | (std::uint32_t(id & 0x03FF) << 16)
                | (std::uint32_t(extra) & 0xFFFF));
    }

    //Version format
    /**
     * 22:31   major   Unsigned 10-bit integer. Major component of version number.
     * 12:22   minor   Unsigned 10-bit integer. Minor component of version number.
     * 0:11    patch   Unsigned 12-bit integer. Patch component of version number.
     */
    static constexpr std::uint32_t makeVersion(std::uint16_t major, std::uint16_t minor, std::uint16_t patch)
    {
        return ((std::uint32_t(major & 0x3FF) << 22) | (std::uint32_t(minor & 0x03FF) << 12)
                | (std::uint32_t(patch) & 0x0FFF));
    }

    static constexpr std::uint32_t SUPPORTED_VERSION[] = {makeVersion(1, 0, 0)}; //array of supported versions

    static constexpr std::uint32_t SUPPORTED_PACKET_MAJOR_VERSION[] = {1}; //array of supported versions

    enum class PacketType : std::uint32_t {
        // transmitted by peer
        ConnectionAckPkt = makePacketType(0, 1),               //packet family=0; packet id=1
        CounterDirectoryReqPkt = makePacketType(0, 3),         //packet family=0; packet id=3
        ActivateTimelineReportingPkt = makePacketType(0, 6),   //packet family=0; packet id=6
        DeactivateTimelineReportingPkt = makePacketType(0, 7), //packet family=0; packet id=7

        // transmitted by target only
        StreamMetadataPkt = makePacketType(0, 0),           //packet family=0; packet id=0
        CounterDirectoryPkt = makePacketType(0, 2),         //packet family=0; packet id=2
        PeriodicCounterCapturePkt = makePacketType(3, 0),   //packet family=3; packet_class = 0, packet_type = 0
        PrePerJobCounterCapturePkt = makePacketType(3, 8),  //packet family=3; packet_class = 1, packet_type = 0
        PostPerJobCounterCapturePkt = makePacketType(3, 9), //packet family=3; packet_class = 1, packet_type = 1
        TimelineMessageDirectoryPkt = makePacketType(1, 0), //packet family=1; packet id=0
        TimelineMessagePkt = makePacketType(1, 1),          //packet family=1; packet id=1

        // transmitted by peer and target
        PeriodicCounterSelectionPkt = makePacketType(0, 4), //packet family=0; packet id=4
        PerJobCounterSelectionPkt = makePacketType(0, 5),   //packet family=0; packet id=5
    };

    //Keeping it at min, can add new return status if needed
    enum class DecodingStatus { Ok, NeedsForwarding, Failed };
    /**
     * Get bits from a given number, between msb and lsb , ([msb, lsb])
     */
    std::uint32_t getBits(std::uint32_t number, int lsb, int msb);
}

#endif /* ARMNN_PACKETUTILITY_H_ */
