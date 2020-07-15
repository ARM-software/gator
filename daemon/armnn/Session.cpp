/**
 * Copyright (C) 2020 by Arm Limited. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "armnn/Session.h"

#include "Logging.h"
#include "PacketDecoderEncoderFactory.h"
#include "armnn/SenderThread.h"
#include "armnn/SessionPacketSender.h"
#include "armnn/SocketIO.h"

#include <cinttypes>
#include <cstring>

static const uint32_t MAGIC = 0x45495434;

namespace armnn {
    std::unique_ptr<Session> Session::create(std::unique_ptr<SocketIO> connection,
                                             IGlobalState & globalState,
                                             ICounterConsumer & counterConsumer)
    {
        logg.logMessage("Creating new ArmNN session");

        HeaderPacket headerPacket {};
        if (!Session::initialiseConnection(*connection, headerPacket)) {
            return nullptr;
        }

        // Decode the metadata packet and create the decoder
        lib::Optional<StreamMetadataContent> streamMetadata =
            getStreamMetadata(headerPacket.streamMetadataPacketBodyAfterMagic, headerPacket.byteOrder);
        if (!streamMetadata) {
            logg.logError("Unable to decode the session metadata. Dropping Session.");
            return nullptr;
        }

        std::unique_ptr<IEncoder> encoder =
            armnn::createEncoder(streamMetadata.get().pktVersionTables, headerPacket.byteOrder);
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
        std::unique_ptr<SessionStateTracker> sst {
            new SessionStateTracker {globalState, counterConsumer, std::move(sps)}};
        std::unique_ptr<IPacketDecoder> decoder =
            armnn::createDecoder(streamMetadata.get().pktVersionTables, headerPacket.byteOrder, *sst);
        if (!decoder) {
            return nullptr;
        }

        return std::unique_ptr<Session> {
            new Session {std::move(connection), headerPacket.byteOrder, std::move(decoder), std::move(sst)}};
    }

    bool Session::initialiseConnection(SocketIO & connection, HeaderPacket & headerPacket)
    {
        // Read meta data and do first time set up.
        if (connection.isOpen()) {
            std::uint8_t header[8];
            std::uint8_t magic[4];

            if (!connection.readExact(header)) {
                // Can't read the header.
                logg.logError("Unable to read the ArmNN metadata packet header");
                return false;
            }

            // Get the byte order
            connection.readExact(magic);
            if (byte_order::get_32<std::uint8_t>(ByteOrder::BIG, magic, 0) == MAGIC) {
                headerPacket.byteOrder = ByteOrder::BIG;
            }
            else if (byte_order::get_32<std::uint8_t>(ByteOrder::LITTLE, magic, 0) == MAGIC) {
                headerPacket.byteOrder = ByteOrder::LITTLE;
            }
            else {
                // invalid magic
                logg.logError("Invalid ArmNN metadata packet magic");
                return false;
            }

            const std::uint32_t streamMetadataIdentifier =
                byte_order::get_32<std::uint8_t>(headerPacket.byteOrder, header, 0);
            if (streamMetadataIdentifier != 0) {
                logg.logError("Invalid ArmNN stream_metadata_identifier (%" PRIu32 ")", streamMetadataIdentifier);
            }

            const std::uint32_t length = byte_order::get_32<std::uint8_t>(headerPacket.byteOrder, header, 4);
            if (length < sizeof(magic)) {
                logg.logError("Invalid ArmNN metadata packet length (%" PRIu32 ")", length);
            }

            std::uint32_t remainingLength = length - sizeof(magic);
            std::vector<std::uint8_t> data(remainingLength);
            if (!connection.readExact(data)) {
                // Can't read the payload
                logg.logError("Unable to read the ArmNN metadata packet payload");
                return false;
            }

            headerPacket.streamMetadataPacketBodyAfterMagic = data;

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

    Session::~Session() { close(); }

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
                logg.logMessage("Session: disconnected due to invalid packet or connection shutdown");
                break;
            }
        }
    }

    bool Session::receiveNextPacket()
    {
        std::uint8_t header[8];
        if (!mConnection->readExact(header)) {
            return false;
        }
        const std::uint32_t type = byte_order::get_32<std::uint8_t>(mEndianness, header, 0);
        const std::uint32_t length = byte_order::get_32<std::uint8_t>(mEndianness, header, 4);
        std::vector<std::uint8_t> data(length);
        if (!mConnection->readExact(data)) {
            return false;
        }

        auto status = mDecoder->decodePacket(type, data);
        return status == DecodingStatus::Ok;
    }
}
