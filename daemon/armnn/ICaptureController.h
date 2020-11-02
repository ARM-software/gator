/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include "armnn/ICounterConsumer.h"

#include <functional>

namespace armnn {

    class ICaptureController {
    public:
        virtual ~ICaptureController() = default;

        /**
         * Should be run once per capture.
         */
        virtual void run(ICounterConsumer & counterConsumer,
                         bool isOneShot,
                         std::function<void()> endSession,
                         std::function<unsigned int()> getBufferBytesAvailable) = 0;

        /**
         * Stop the current run
         */
        virtual void interrupt() = 0;
    };

}
