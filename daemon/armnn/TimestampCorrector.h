/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include "IBlockCounterMessageConsumer.h"
#include "armnn/ICounterConsumer.h"

#include <memory>
#include <set>

namespace armnn {
    class IFrameBuilderFactory;

    /**
     *  An implementation of ICounterConsumer which corrects a monotonic timestamp by
     *  converting it to a delta of monotonic start
     **/
    class TimestampCorrector : public ICounterConsumer {
    public:
        TimestampCorrector(IFrameBuilderFactory & frameBuilderFactory, std::uint64_t monotonicStarted);

        bool consumeCounterValue(std::uint64_t timestamp,
                                 ApcCounterKeyAndCoreNumber keyAndCore,
                                 std::uint32_t counterValue) override;

        bool consumePacket(std::uint32_t sessionId, lib::Span<const std::uint8_t> data) override;

    private:
        IFrameBuilderFactory & mFrameBuilderFactory;
        std::uint64_t monotonicStarted;
        std::set<int> fdsStarted {};
        std::unique_ptr<IBlockCounterMessageConsumer> counterConsumer {};
    };
}
