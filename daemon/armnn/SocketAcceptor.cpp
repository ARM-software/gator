/**
 * Copyright (C) 2020-2021 by Arm Limited. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "armnn/SocketAcceptor.h"

namespace armnn {
    std::unique_ptr<ISession> SocketAcceptor::accept()
    {
        while (true) {
            std::unique_ptr<SocketIO> socket = mAcceptingSocket.accept(-1);
            if (socket == nullptr) {
                return nullptr;
            }
            auto session = mSupplier(std::move(socket));
            if (session != nullptr) {
                return session;
            }
        }
    }

    void SocketAcceptor::interrupt() { mAcceptingSocket.interrupt(); }
}
