/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#pragma once

#include "armnn/ISocketIO.h"

#include <memory>

namespace armnn {

    class ISocketIOConsumer {
    public:
        virtual void consumeSocket(std::unique_ptr<ISocketIO> ptr) = 0;
    };

}
