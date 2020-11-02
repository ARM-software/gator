/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include <cstdint>
#include <set>
#include <vector>

namespace armnn {
    class IEncoder {
    public:
        virtual ~IEncoder() = default;

        /**
         * @param - period  - representing the rate at which periodic sampling is performed in microsecond
         * @param - eventUids, list of event uids
         * if eventUids is empty and disable collection packet bytes are generated
         * @return - return header for the request
         */
        virtual std::vector<std::uint8_t> encodePeriodicCounterSelectionRequest(
            std::uint32_t period,
            const std::set<std::uint16_t> & eventUids) = 0;

        /**
         * @param - objectId - representing the ID of the object that the job is associated with.
         * @param - eventUids, list of event uids
         * if eventUids is empty and disable collection packet bytes are generated
         * @return - return header for the request
         */
        virtual std::vector<std::uint8_t> encodePerJobCounterSelectionRequest(
            std::uint64_t objectId,
            const std::set<std::uint16_t> & eventUids) = 0;

        /**
         * acknowledge that a valid connection has been established.
         * It shall be transmitted immediately after the Stream MetaData Packet has been received and processed.
         * Note For version 1.0.0 , the data length is 0
         */
        virtual std::vector<std::uint8_t> encodeConnectionAcknowledge() = 0;

        /**
         * This request can be issued at any time after a connection has been established
         * Note: For version 1.0.0 , the data length is 0
         */
        virtual std::vector<std::uint8_t> encodeCounterDirectoryRequest() = 0;

        /**
         * @returns a packet to be sent by the peer to the target that requests for the timeline
         *          reporting to be activated
         * Note: For version 1.0.0, the data length of this packet is always 0
         **/
        virtual std::vector<std::uint8_t> encodeActivateTimelineReportingPacket() = 0;

        /**
         * @returns a packet to be sent by the peer to the target that requests for the timeline
         *          reporting to be deactivated
         * Note: For version 1.0.0, the data length of this packet is always 0
         **/
        virtual std::vector<std::uint8_t> encodeDeactivateTimelineReportingPacket() = 0;
    };
}
