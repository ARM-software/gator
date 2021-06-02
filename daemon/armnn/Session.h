/**
 * Copyright (C) 2020-2021 by Arm Limited. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#pragma once

#include "armnn/ByteOrder.h"
#include "armnn/IGlobalState.h"
#include "armnn/IPacketDecoder.h"
#include "armnn/ISender.h"
#include "armnn/ISession.h"
#include "armnn/ISessionPacketSender.h"
#include "armnn/SessionStateTracker.h"
#include "armnn/SocketIO.h"

#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

namespace armnn {
    // Struct to store the metadata for the connection
    struct HeaderPacket {
        ByteOrder byteOrder;
        std::vector<std::uint8_t> packet;
    };

    class Session : public ISession {
    public:
        /** Creates a unique pointer to a Session object **/
        static std::unique_ptr<Session> create(std::unique_ptr<SocketIO> connection,
                                               IGlobalState & globalState,
                                               ICounterConsumer & counterConsumer,
                                               const std::uint32_t sessionID);

        /**
         * Initialises the connection.
         * @param connection connection to be initialised
         * @param headerPacket out parameter for a HeaderPacket
         * @return true if connection has been initialised, false if not
         **/
        static bool initialiseConnection(SocketIO & connection, HeaderPacket & headerPacket);

        /**
         * @param connection will need to be initialised prior
         * @param decoder will outlive sst
         * @param sst will outlive socket
         */
        Session(std::unique_ptr<SocketIO> connection,
                ByteOrder byteOrder,
                std::unique_ptr<IPacketDecoder> decoder,
                std::unique_ptr<SessionStateTracker> sst);

        Session() = delete;
        ~Session() override;

        // No copying
        Session(const Session &) = delete;
        Session & operator=(const Session &) = delete;

        // No moving
        Session(Session && that) = delete;
        Session & operator=(Session && that) = delete;

        /** Run the read loop.
         *  Will loop until an invalid packet is recieved
         **/
        void runReadLoop() override;

        /** Closes the connection **/
        void close() override;

        /** Enable the capture **/
        bool enableCapture() override { return mSessionStateTracker->doEnableCapture(); }

        /** Disable the capture **/
        bool disableCapture() override { return mSessionStateTracker->doDisableCapture(); }

    private:
        static const std::size_t TIMEOUT = 3000;
        const ByteOrder mEndianness;
        // the order of these is important because they hold references to each other
        std::unique_ptr<SocketIO> mConnection;
        std::unique_ptr<SessionStateTracker> mSessionStateTracker;
        std::unique_ptr<IPacketDecoder> mDecoder;

        bool receiveNextPacket();
    };
}
