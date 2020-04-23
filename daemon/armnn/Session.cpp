/**
 * Copyright (C) 2020 by Arm Limited. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "armnn/Session.h"
#include "Logging.h"
#include "armnn/SocketIO.h"
#include "armnn/SenderThread.h"
#include <cstring>

#include "PacketDecoderEncoderFactory.h"

namespace armnn
{
    std::unique_ptr<Session> Session::create(std::unique_ptr<SocketIO> connection, IPacketConsumer & consumer)
    {

        HeaderPacket headerPacket{};
        if(!Session::initialiseConnection(*connection, headerPacket))
        {
            // Print the last error after receiving a null pointer from this function.
            return nullptr;
        }

        // Decode the metadata packet and create the decoder
        lib::Optional<StreamMetadataContent> streamMetadata = getStreamMetadata(headerPacket.data, headerPacket.byteOrder);
        if(!streamMetadata)
        {
            return nullptr;
        }

        std::unique_ptr<IPacketDecoder> decoder = armnn::createDecoder(streamMetadata.get().pktVersionTables,
                                                                     headerPacket.byteOrder,
                                                                     consumer);
        if (!decoder) {
            return nullptr;
        }

        return std::unique_ptr<Session>{ new Session{std::move(connection), headerPacket.byteOrder, std::move(decoder)}};
    }

    bool Session::initialiseConnection(SocketIO & connection, HeaderPacket & headerPacket)
    {
        // Read meta data and do first time set up.
        if (connection.isOpen())
        {
            std::uint8_t header[8];
            std::uint8_t magic[4];

            if (!connection.readExact(header))
            {
                // Can't read the header.
                logg.logError("Unable to read the ArmNN metadata packet header");
                return false;
            }
            headerPacket.firstHeaderWord = header[3] | (header[2] << 8) | (header[1] << 16) | (header[0] << 24);

            // Get the byte order
            connection.readExact(magic);
            std::uint32_t magicWord = magic[3] | (magic[2] << 8) | (magic[1] << 16) | (magic[0] << 24);
            if (magicWord == MAGIC_BE)
            {
                headerPacket.byteOrder = ByteOrder::BIG;
            }
            else if (magicWord == MAGIC_LE)
            {
                headerPacket.byteOrder =  ByteOrder::LITTLE;
            }
            else
            {
                // invalid magic
                logg.logError("Invalid ArmNN metadata packet magic");
                return false;
            }

            uint32_t dataLength = header[7] | (header[6] << 8) | (header[5] << 16) | (header[4] << 24);
            headerPacket.length = byte_order::convertEndianness(headerPacket.byteOrder, dataLength);

            // The remainingLength of the data is the length (header[1]) minus
            // the legth of the magic.
            std::uint32_t remainingLength = (headerPacket.length > 4 ? headerPacket.length - 4: 0);
            std::vector<std::uint8_t> data(remainingLength);
            if (!connection.readExact(data))
            {
                // Can't read the payload
                logg.logError("Unable to read the ArmNN metadata packet payload");
                return false;
            }

            headerPacket.data = data;

            return true;
        }

        return false;
    }

    Session::Session(std::unique_ptr<SocketIO> connection, ByteOrder byteOrder, std::unique_ptr<IPacketDecoder>  decoder) :
        mEndianness{byteOrder},
        mSender{new SenderThread{*connection}},
        mConnection{std::move(connection)},
        mSessionClosed{false},
        mDecoder{std::move(decoder)}
    {
    }

    Session::~Session()
    {
        close();
    }

    void Session::close()
    {
        mSender->stopSending();
        if (mConnection->isOpen())
        {
            mConnection->close();
        }
        mSessionClosed = true;
    }

    void Session::readLoop()
    {
        // Main reading loop, decode packets
        while (mConnection->isOpen())
        {
            if (!receiveNextPacket())
            {
                disconnectInvalidPacket();
                break;
            }
        }

        logg.logMessage("Exit read loop");
    }

    bool Session::receiveNextPacket()
    {
        std::uint8_t header[8];
        if (!mConnection->readExact(header))
        {
            return false;
        }
        uint32_t dataLength = header[7] | (header[6] << 8) | (header[5] << 16) | (header[4] << 24);
        std::uint32_t length = byte_order::convertEndianness(mEndianness, dataLength);
        std::vector<std::uint8_t> data(length);
        if (!mConnection->readExact(data))
        {
            return false;
        }

        mDecoder->decodePacket(header[0], data);

        return true;
    }

    void Session::disconnectInvalidPacket()
    {
        logg.logError("Invalid packet received, disconnecting");
        close();
    }
}
