/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include "../Source.h"
#include "Buffer.h"
#include "ICaptureController.h"

namespace armnn {
    class Source : public ::Source {
    public:
        Source(Child & child,
               ICaptureController & captureController,
               sem_t & readerSem,
               std::function<std::int64_t()> getMonotonicStarted);

        virtual bool prepare() override;
        virtual void run() override;
        virtual void interrupt() override;
        virtual bool isDone() override;
        virtual void write(::ISender & sender) override;

    private:
        ICaptureController & captureController;
        Buffer buffer;
        std::function<std::int64_t()> mGetMonotonicStarted;
    };
}
