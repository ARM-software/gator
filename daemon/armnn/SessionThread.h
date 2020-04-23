/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include "armnn/SocketIO.h"
#include "Session.h"

#include <thread>

namespace armnn
{
    class SessionThread
    {
    public:
        SessionThread(std::unique_ptr<Session> session);
        SessionThread() = delete;
        ~SessionThread();

        // No copying
        SessionThread(const SessionThread &) = delete;
        SessionThread & operator=(const SessionThread &) = delete;

        // No moving
        SessionThread(SessionThread && that) = delete;
        SessionThread& operator=(SessionThread&& that) = delete;

        /** Run the reader thread **/
        void run();

        /** Joins the session thread **/
        void join();

        std::unique_ptr<Session> mSession;

    private:
        std::thread mSessionThread;
    };
}
