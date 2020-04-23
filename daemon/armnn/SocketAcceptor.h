/**
 * Copyright (C) 2020 by Arm Limited. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ARMNN_SOCKET_ACCEPTOR_H
#define ARMNN_SOCKET_ACCEPTOR_H

#include <functional>
#include "armnn/SocketIO.h"

namespace armnn
{
    using SocketIOConsumer = std::function<void(std::unique_ptr<SocketIO>)>;

    class SocketAcceptor
    {
    public:

        SocketAcceptor(SocketIO & socket, SocketIOConsumer consumer) :
            mAcceptingSocket(socket),
            mConsumer(consumer)
        {

        }
        bool acceptOne();

    private:
        SocketIO & mAcceptingSocket;
        SocketIOConsumer mConsumer;
    };

}

#endif
