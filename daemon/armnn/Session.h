/**
 * Copyright (C) 2020 by Arm Limited. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#pragma once

#include "armnn/SocketIO.h"
#include "armnn/ByteOrder.h"
#include "armnn/ISender.h"
#include "armnn/IPacketDecoder.h"
#include "armnn/IPacketConsumer.h"

#include <mutex>
#include <atomic>
#include <thread>
#include <queue>
#include <utility>

namespace armnn
{
    // Struct to store the metadata for the connection
    struct HeaderPacket
    {
        std::uint32_t firstHeaderWord;
        std::uint32_t length;
        ByteOrder byteOrder;
        std::vector<std::uint8_t> data;
    };

    class Session
    {
    public:
        /** Creates a unique pointer to a Session object **/
        static std::unique_ptr<Session> create(std::unique_ptr<SocketIO> connection, IPacketConsumer & consumer);

        /**
         * Initialises the connection.
         * @param connection: connection to be initialised
         * @param headerPacket: out parameter for a HeaderPacket
         * @return true if connection has been initialised, false if not
         **/
        static bool initialiseConnection(SocketIO & connection, HeaderPacket & headerPacket);

        Session() = delete;
        ~Session();

        // No copying
        Session(const Session &) = delete;
        Session & operator=(const Session &) = delete;

        // No moving
        Session(Session && that) = delete;
        Session& operator=(Session&& that) = delete;

        /** Run the read loop **/
        void readLoop();

        /** Closes the connection **/
        void close();

        /** @return The byte order of the client connection **/
        ByteOrder getEndianness() const { return mEndianness; }

        /** @return Whether the session is closed or not **/
        bool isSessionClosed() const { return mSessionClosed; }

    private:
        // Private constructor, use factory create method
        Session(std::unique_ptr<SocketIO> socket, ByteOrder byteOrder, std::unique_ptr<IPacketDecoder> decoder);

        static const uint32_t MAGIC_BE = 0x45495434;
        static const uint32_t MAGIC_LE = 0x34544945;
        static const std::size_t TIMEOUT = 3000;
        const ByteOrder mEndianness;
        std::unique_ptr<ISender> mSender;
        std::unique_ptr<SocketIO> mConnection;
        std::atomic_bool mSessionClosed;
        std::unique_ptr<IPacketDecoder> mDecoder;

        bool receiveNextPacket();
        void disconnectInvalidPacket();
    };
}
