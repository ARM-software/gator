/* Copyright (C) 2020-2023 by Arm Limited. All rights reserved. */

#include "armnn/TimestampCorrector.h"

#include "armnn/ICounterConsumer.h"
#include "armnn/IFrameBuilderFactory.h"
#include "lib/Span.h"

#include <cstdint>

namespace armnn {
    TimestampCorrector::TimestampCorrector(IFrameBuilderFactory & frameBuilderFactory, std::uint64_t monotonicStarted)
        : mFrameBuilderFactory {frameBuilderFactory}, monotonicStarted {monotonicStarted}
    {
    }

    bool TimestampCorrector::consumeCounterValue(std::uint64_t timestamp,
                                                 ApcCounterKeyAndCoreNumber keyAndCore,
                                                 std::uint32_t counterValue)
    {
        // Only pass on the counter value if it is from after monotonic start
        if (timestamp >= monotonicStarted) {
            if (!counterConsumer) {
                // begin a new block counter frame
                counterConsumer = mFrameBuilderFactory.createBlockCounterFrame();
            }
            return counterConsumer->counterMessage(timestamp - monotonicStarted,
                                                   keyAndCore.core,
                                                   keyAndCore.key,
                                                   counterValue);
        }
        // The value was successfully consumed but has been dropped because the timestamp was too early
        return true;
    }
    bool TimestampCorrector::consumePacket(std::uint32_t sessionId, lib::Span<const std::uint8_t> data)
    {
        // finish any in progess frame before starting a new one
        counterConsumer.reset();

        // real FDs are small positive numbers and used by ExternalSource
        // -1 has special meaning that an fd is closed
        // so use anything below that.
        const int fd = -2 - sessionId;

        if (fdsStarted.count(fd) == 0) {
            const std::uint8_t ESTATE_ARMNN[] {'A', 'R', 'M', 'N', 'N', '_', 'V', '1', '\n'};
            mFrameBuilderFactory.createExternalFrame(fd, ESTATE_ARMNN);
            fdsStarted.insert(fd);
        }

        mFrameBuilderFactory.createExternalFrame(fd, data);

        return true;
    }
}
