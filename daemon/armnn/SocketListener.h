/**
 * Copyright (C) 2020 by Arm Limited. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ARMNN_SOCKETACCEPTORTHREAD_H
#define ARMNN_SOCKETACCEPTORTHREAD_H

#include <thread>
#include "armnn/SocketAcceptor.h"

namespace armnn
{
    void listenForConnections(SocketIOConsumer consumer);
}

#endif /* ARMNN_SOCKETACCEPTORTHREAD_H */
