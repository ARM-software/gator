/* Copyright (C) 2020-2021 by Arm Limited. All rights reserved. */

#pragma once

#include "armnn/ISender.h"
#include "armnn/SenderQueue.h"
#include "armnn/SocketIO.h"

#include <thread>

namespace armnn {
    class SenderThread : public ISender {
    public:
        SenderThread(SocketIO & connection);
        SenderThread() = delete;
        ~SenderThread() override;

        // No copying
        SenderThread(const SenderThread &) = delete;
        SenderThread & operator=(const SenderThread &) = delete;

        // No moving
        SenderThread(SenderThread && that) = delete;
        SenderThread & operator=(SenderThread && that) = delete;

        /**
         * Adds a packet to the sender queue
         * @param data the packet to send
         * @return whether the add was successful or not.
         **/
        bool send(std::vector<std::uint8_t> && data) override;

    private:
        std::unique_ptr<SenderQueue> mSenderQueue;
        std::thread mSenderThread;

        void run();
    };
}
