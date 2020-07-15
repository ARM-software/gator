/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */
#pragma once

#include "armnn/ISession.h"

#include <memory>

namespace armnn {
    class IAcceptor {
    public:
        virtual ~IAcceptor() = default;

        /**
         * Calls accept on a socket (blocks until a new socket is accepted)
         * @return an ISession object, or nullptr if no more ISession objects cannot be constructed
         *          (if the accepting socket encounters an error)
         **/
        virtual std::unique_ptr<ISession> accept() = 0;

        /**
         * Should interrupt the accept function if it is blocking and cause it to return nullptr
         **/
        virtual void interrupt() = 0;
    };
}
