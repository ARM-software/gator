/**
 * Copyright (C) 2020 by Arm Limited. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ARMNN_SOCKET_ACCEPTOR_H
#define ARMNN_SOCKET_ACCEPTOR_H

#include "armnn/IAcceptor.h"
#include "armnn/ISession.h"
#include "armnn/SocketIO.h"

#include <functional>
#include <utility>

namespace armnn {
    /// Input will not be nullptr
    /// May return nullptr if a session could not be created from the socket
    using SessionSupplier = std::function<std::unique_ptr<ISession>(std::unique_ptr<SocketIO>)>;

    class SocketAcceptor : public IAcceptor {
    public:
        SocketAcceptor(SocketIO & socket, SessionSupplier supplier)
            : mAcceptingSocket(socket), mSupplier(std::move(supplier))
        {
        }

        std::unique_ptr<ISession> accept() override;
        void interrupt() override;

    private:
        SocketIO & mAcceptingSocket;
        SessionSupplier mSupplier;
    };

}

#endif
