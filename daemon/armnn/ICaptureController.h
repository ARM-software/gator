/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include "ICounterConsumer.h"

#include <functional>

namespace armnn {

    class ICaptureController {
    public:
        virtual ~ICaptureController() = default;

        /**
         * Should be run once per capture.
         */
        virtual void run(ICounterConsumer & counterConsumer) = 0;

        /**
         * Stop the current run
         */
        virtual void interrupt() = 0;
    };

}
