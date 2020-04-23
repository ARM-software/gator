/**
 * Copyright (C) 2020 by Arm Limited. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "armnn/SocketAcceptor.h"

namespace armnn
{
    bool SocketAcceptor::acceptOne()
    {
        std::unique_ptr<SocketIO> ptr = mAcceptingSocket.accept(-1);
        if (ptr == nullptr)
        {
            return false;
        }
        else
        {
            mConsumer(std::move(ptr));
            return true;
        }
    }
}
