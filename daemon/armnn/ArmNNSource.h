/* Copyright (C) 2020-2022 by Arm Limited. All rights reserved. */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <semaphore.h>

class Child;
class Source;
namespace armnn {
    class ICaptureController;
    std::shared_ptr<::Source> createSource(ICaptureController & captureController, sem_t & readerSem);
}
