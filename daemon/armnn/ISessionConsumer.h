/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#pragma once

#include "armnn/ISession.h"

#include <memory>

namespace armnn {
    /**
     * Interface for something that consumes and manages newly started sessions
     */
    class ISessionConsumer {
    public:
        virtual ~ISessionConsumer() noexcept = default;

        [[nodiscard]] virtual bool acceptSession(std::unique_ptr<ISession> session) = 0;
    };
}
