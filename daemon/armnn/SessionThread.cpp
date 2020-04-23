/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#include "SessionThread.h"
#include "Logging.h"
#include "SocketIO.h"

#include <cstring>

namespace armnn
{
    SessionThread::SessionThread(std::unique_ptr<Session> session) :
        mSession{std::move(session)},
        mSessionThread{&SessionThread::run, this}
    {
    }

    SessionThread::~SessionThread()
    {
        join();
    }

    void SessionThread::run()
    {
        mSession->readLoop();
    }

    void SessionThread::join()
    {
        mSessionThread.join();
    }
}
