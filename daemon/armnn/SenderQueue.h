/* Copyright (C) 2020-2021 by Arm Limited. All rights reserved. */

#pragma once

#include "armnn/SocketIO.h"

#include <condition_variable>
#include <mutex>
#include <queue>

namespace armnn {
    class SenderQueue {
    public:
        SenderQueue(SocketIO & connection);
        SenderQueue() = delete;

        // No copying
        SenderQueue(const SenderQueue &) = delete;
        SenderQueue & operator=(const SenderQueue &) = delete;

        /** Run the send loop **/
        void sendLoop();

        /**
         * Adds a packet to the sender queue
         * @param data the packet to send
         * @return whether the add was successful or not.
         **/
        bool add(std::vector<std::uint8_t> && data);

        /**
         * Stop the thread from sending
         * */
        void stopSending();

        /**
         * Send the item to the socket (Not thread safe)
         * NOTE: use add method instead.
         * @param data data to be sent
         **/
        void sendItem(std::vector<std::uint8_t> && data);

    private:
        SocketIO & mConnection;
        std::mutex mQueueMutex;
        std::condition_variable mConditionVar;
        bool mSendFinished;
        std::queue<std::vector<std::uint8_t>> mQueue;

        void unableToSendItem();
    };
}
