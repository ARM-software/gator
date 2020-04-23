/**
 * Copyright (C) 2020 by Arm Limited. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "armnn/SocketIO.h"
#include "armnn/SocketAcceptorLoop.h"
#include "armnn/SocketListener.h"

namespace armnn
{
    void listenForConnections(SocketIOConsumer consumer)
    {
        SocketIO serverSocket = SocketIO::udsServerListen("\0armnn", false);
        SocketAcceptor acceptor{serverSocket, consumer};
        SocketAcceptorLoop loop(acceptor);
        loop.acceptLoop();
    }

}
