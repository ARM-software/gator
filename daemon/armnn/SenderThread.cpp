/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#include "SenderThread.h"
#include "Logging.h"
#include "SocketIO.h"

#include <cstring>

namespace armnn
{
    SenderThread::SenderThread(SocketIO & connection) :
        mSenderQueue{new SenderQueue{connection}},
        mSenderThread{&SenderThread::run, this}
    {
    }

    SenderThread::~SenderThread()
    {
        stopSending();
    }

    bool SenderThread::send(std::vector<std::uint8_t> && data)
    {
        return mSenderQueue->add(std::move(data));
    }

    void SenderThread::stopSending()
    {
        mSenderQueue->stopSending();
        // Don't need the thread after sending has stopped.
        mSenderThread.join();
    }

    void SenderThread::run()
    {
        mSenderQueue->sendLoop();
    }
}
