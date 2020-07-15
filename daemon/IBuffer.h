/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#ifndef IBUFFER_H_
#define IBUFFER_H_

#include "IBlockCounterFrameBuilder.h"
#include "IBlockCounterMessageConsumer.h"
#include "IBufferControl.h"
#include "IRawFrameBuilder.h"

#include <cstdint>

class IBuffer : public IBufferControl,
                public IRawFrameBuilder,
                public IBlockCounterFrameBuilder,
                public IBlockCounterMessageConsumer {
public:
    bool isFull() const override { return bytesAvailable() <= 0; }
};

#endif /* IBUFFER_H_ */
