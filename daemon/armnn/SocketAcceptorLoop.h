/**
 * Copyright (C) 2020 by Arm Limited. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ARMNN_SOCKET_ACCEPTOR_LOOP_H
#define ARMNN_SOCKET_ACCEPTOR_LOOP_H

#include "armnn/SocketAcceptor.h"

namespace armnn
{
    class SocketAcceptorLoop
    {
    public:
        SocketAcceptorLoop(SocketAcceptor & acceptor) :
            mAcceptor(acceptor)
        {
        }

        void acceptLoop();

    private:
        SocketAcceptor & mAcceptor;
    };

}

#endif
