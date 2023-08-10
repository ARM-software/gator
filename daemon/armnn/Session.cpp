/**
 * Copyright (C) 2020-2023 by Arm Limited. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "armnn/Session.h"

#include "Logging.h"
#include "armnn/PacketDecoderEncoderFactory.h"
#include "armnn/SenderThread.h"
#include "armnn/SessionPacketSender.h"
#include "armnn/SocketIO.h"

#include <cinttypes>
#include <cstring>

static constexpr uint32_t MAGIC = 0x45495434;
static constexpr std::size_t HEADER_SIZE = 8;
static constexpr std::size_t MAGIC_SIZE = 4;

namespace armnn {
    std::unique_ptr<Session> Session::create(std::unique_ptr<SocketIO> connection,
                                             IGlobalState & globalState,
                                             ICounterConsumer & counterConsumer,
                                             const std::uint32_t sessionID)
    {
        LOG_FINE("Creating new ArmNN session");

        HeaderPacket headerPacket {};
        if (!Session::initialiseConnection(*connection, headerPacket)) {
            return nullptr;
        }

        // Decode the metadata packet and create the decoder
        const auto packetBodyAfterMagic = lib::makeConstSpan(headerPacket.packet).subspan(HEADER_SIZE + MAGIC_SIZE);
        std::optional<StreamMetadataContent> streamMetadata =
            getStreamMetadata(packetBodyAfterMagic, headerPacket.byteOrder);
        if (!streamMetadata) {
            LOG_ERROR("Unable to decode the session metadata. Dropping Session.");
            return nullptr;
        }

        std::unique_ptr<IEncoder> encoder =
            armnn::createEncoder(streamMetadata->pktVersionTables, headerPacket.byteOrder);
        if (!encoder) {
            return nullptr;
        }

        std::vector<std::uint8_t> ack = encoder->encodeConnectionAcknowledge();
        if (!connection->writeExact(ack)) {
            return nullptr;
        }

        // Create the SessionPacketSender (all the sending part of the Session)
        std::unique_ptr<ISender> sender {new SenderThread {*connection}};
        std::unique_ptr<ISessionPacketSender> sps {new SessionPacketSender {std::move(sender), std::move(encoder)}};

        // Create the SST and decoder.
        std::unique_ptr<SessionStateTracker> sst {new SessionStateTracker {globalState,
                                                                           counterConsumer,
                                                                           std::move(sps),
                                                                           sessionID,
                                                                           std::move(headerPacket.packet)}};

        std::unique_ptr<IPacketDecoder> decoder =
            armnn::createDecoder(streamMetadata->pktVersionTables, headerPacket.byteOrder, *sst);
        if (!decoder) {
            return nullptr;
        }

        return std::make_unique<Session>(std::move(connection),
                                         headerPacket.byteOrder,
                                         std::move(decoder),
                                         std::move(sst));
    }

    bool Session::initialiseConnection(SocketIO & connection, HeaderPacket & headerPacket)
    {
        // Read meta data and do first time set up.
        if (connection.isOpen()) {
            std::vector<std::uint8_t> packet(HEADER_SIZE + MAGIC_SIZE);

            if (!connection.readExact(packet)) {
                // Can't read the header.
                LOG_ERROR("Unable to read the ArmNN metadata packet header");
                return false;
            }

            // Get the byte order
            if (byte_order::get_32<std::uint8_t>(ByteOrder::BIG, packet, HEADER_SIZE) == MAGIC) {
                headerPacket.byteOrder = ByteOrder::BIG;
            }
            else if (byte_order::get_32<std::uint8_t>(ByteOrder::LITTLE, packet, HEADER_SIZE) == MAGIC) {
                headerPacket.byteOrder = ByteOrder::LITTLE;
            }
            else {
                // invalid magic
                LOG_ERROR("Invalid ArmNN metadata packet magic");
                return false;
            }

            const std::uint32_t streamMetadataIdentifier =
                byte_order::get_32<std::uint8_t>(headerPacket.byteOrder, packet, 0);
            if (streamMetadataIdentifier != 0) {
                LOG_ERROR("Invalid ArmNN stream_metadata_identifier (%" PRIu32 ")", streamMetadataIdentifier);
                return false;
            }

            const std::uint32_t length = byte_order::get_32<std::uint8_t>(headerPacket.byteOrder, packet, 4);
            if (length < MAGIC_SIZE) {
                LOG_ERROR("Invalid ArmNN metadata packet length (%" PRIu32 ")", length);
                return false;
            }

            std::uint32_t remainingLength = length - MAGIC_SIZE;
            std::vector<std::uint8_t> restOfPacketBody(remainingLength);
            if (!connection.readExact(restOfPacketBody)) {
                // Can't read the payload
                LOG_ERROR("Unable to read the ArmNN metadata packet payload");
                return false;
            }

            // Construct the body of the packet
            packet.insert(packet.end(), restOfPacketBody.begin(), restOfPacketBody.end());
            headerPacket.packet = std::move(packet);

            return true;
        }

        return false;
    }

    Session::Session(std::unique_ptr<SocketIO> connection,
                     ByteOrder byteOrder,
                     std::unique_ptr<IPacketDecoder> decoder,
                     std::unique_ptr<SessionStateTracker> sst)
        : mEndianness {byteOrder},
          mConnection {std::move(connection)},
          mSessionStateTracker {std::move(sst)},
          mDecoder {std::move(decoder)}
    {
    }

    Session::~Session()
    {
        close();
    }

    void Session::close()
    {
        if (mConnection->isOpen()) {
            mConnection->interrupt();
        }
    }

    void Session::runReadLoop()
    {
        // Main reading loop, decode packets
        while (true) {
            if (!receiveNextPacket()) {
                LOG_DEBUG("Session: disconnected due to invalid packet or connection shutdown");
                break;
            }
        }
    }

    bool Session::receiveNextPacket()
    {
        std::vector<std::uint8_t> packet(HEADER_SIZE);
        if (!mConnection->readExact(packet)) {
            return false;
        }
        const std::uint32_t type = byte_order::get_32<std::uint8_t>(mEndianness, packet, 0);
        const std::uint32_t length = byte_order::get_32<std::uint8_t>(mEndianness, packet, 4);
        packet.resize(HEADER_SIZE + length);
        const auto data = lib::makeSpan(packet).subspan(HEADER_SIZE);
        if (!mConnection->readExact(data)) {
            return false;
        }

        auto status = mDecoder->decodePacket(type, data);
        if (status == DecodingStatus::NeedsForwarding) {
            return mSessionStateTracker->forwardPacket(packet);
        }

        return status == DecodingStatus::Ok;
    }
}
