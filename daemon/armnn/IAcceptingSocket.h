/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#pragma once

#include "armnn/ISocketIO.h"

#include <memory>

namespace armnn {

    class IAcceptingSocket {
    public:
        virtual ~IAcceptingSocket() = default;

        /**
         * Calls accept on a socket (blocks until a new socket is accepted)
         * @return an ISocketIO object, or nullptr if no more can be accepted
         *          (if the accepting socket encounters an error)
         **/
        [[nodiscard]] virtual std::unique_ptr<ISocketIO> accept(int timeout) = 0;

        /**
         * Should interrupt the accept function if it is blocking and cause it to return nullptr
         **/
        virtual void interrupt() = 0;
    };
}
