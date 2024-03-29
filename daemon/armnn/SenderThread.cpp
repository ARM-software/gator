/* Copyright (C) 2020-2023 by Arm Limited. All rights reserved. */

#include "armnn/SenderThread.h"

#include "armnn/ISocketIO.h"
#include "armnn/SenderQueue.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace armnn {
    SenderThread::SenderThread(ISocketIO & connection)
        : mSenderQueue {new SenderQueue {connection}}, mSenderThread {&SenderThread::run, this}
    {
    }

    SenderThread::~SenderThread()
    {
        mSenderQueue->stopSending();
        mSenderThread.join();
    }

    bool SenderThread::send(std::vector<std::uint8_t> && data)
    {
        return mSenderQueue->add(std::move(data));
    }

    void SenderThread::run()
    {
        mSenderQueue->sendLoop();
    }
}
