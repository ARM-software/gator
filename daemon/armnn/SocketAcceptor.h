/**
 * Copyright (C) 2020-2023 by Arm Limited. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ARMNN_SOCKET_ACCEPTOR_H
#define ARMNN_SOCKET_ACCEPTOR_H

#include "armnn/IAcceptor.h"
#include "armnn/ISession.h"
#include "armnn/ISocketIO.h"
#include "armnn/SocketIO.h"

#include <functional>
#include <utility>

namespace armnn {
    /// Input will not be nullptr
    /// May return nullptr if a session could not be created from the socket
    using SessionSupplier = std::function<std::unique_ptr<ISession>(std::unique_ptr<ISocketIO>)>;

    template<typename T>
    class SocketAcceptor : public IAcceptor {
    public:
        SocketAcceptor(T & socket, SessionSupplier supplier) : mAcceptingSocket(socket), mSupplier(std::move(supplier))
        {
        }

        std::unique_ptr<ISession> accept() override
        {
            while (true) {
                std::unique_ptr<ISocketIO> socket = mAcceptingSocket.accept(-1);
                if (socket == nullptr) {
                    return nullptr;
                }
                auto session = mSupplier(std::move(socket));
                if (session != nullptr) {
                    return session;
                }
            }
        }

        void interrupt() override { mAcceptingSocket.interrupt(); }

    private:
        T & mAcceptingSocket;
        SessionSupplier mSupplier;
    };

}

#endif
