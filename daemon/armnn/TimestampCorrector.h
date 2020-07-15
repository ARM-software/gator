/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include "IBlockCounterMessageConsumer.h"
#include "armnn/ICounterConsumer.h"

#include <functional>

namespace armnn {
    /**
     *  An implementation of ICounterConsumer which corrects a monotonic timestamp by
     *  converting it to a delta of monotonic start
     **/
    class TimestampCorrector : public ICounterConsumer {
    public:
        TimestampCorrector(IBlockCounterMessageConsumer & buffer, std::function<std::int64_t()> getMonotonicStarted);

        bool consumerCounterValue(std::uint64_t timestamp,
                                  ApcCounterKeyAndCoreNumber keyAndCore,
                                  std::uint32_t counterValue) override;

    private:
        IBlockCounterMessageConsumer & mBuffer;
        std::function<std::int64_t()> mGetMonotonicStarted;
    };
}