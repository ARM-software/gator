/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#include "armnn/TimestampCorrector.h"

#include <utility>

namespace armnn {
    TimestampCorrector::TimestampCorrector(IBlockCounterMessageConsumer & buffer,
                                           std::function<std::int64_t()> getMonotonicStarted)
        : mBuffer {buffer}, mGetMonotonicStarted {std::move(getMonotonicStarted)}
    {
    }

    bool TimestampCorrector::consumerCounterValue(std::uint64_t timestamp,
                                                  ApcCounterKeyAndCoreNumber keyAndCore,
                                                  std::uint32_t counterValue)
    {
        std::int64_t monotonicStartTime = mGetMonotonicStarted();
        std::int64_t relativeOffset = static_cast<std::int64_t>(timestamp) - monotonicStartTime;

        // Only pass on the counter value if it is from after monotonic start
        if ((monotonicStartTime != -1) && (relativeOffset >= 0)) {
            return mBuffer.counterMessage(relativeOffset, keyAndCore.core, keyAndCore.key, counterValue);
        }
        else {
            // The value was successfully consumed but has been dropped because the timestamp was too early
            return true;
        }
    }
}
