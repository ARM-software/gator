/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include <cstdint>

namespace armnn {
    /** Tuple of (key, core) where 'key' is the APC counter key identifier, core is the core number associated with the counter */
    struct ApcCounterKeyAndCoreNumber {
        int key;
        unsigned core;

        inline bool operator==(ApcCounterKeyAndCoreNumber that) const
        {
            return (key == that.key) && (core == that.core);
        }
    };

    class ICounterConsumer {
    public:
        virtual ~ICounterConsumer() = default;

        /**
         * @return whether the value was successfully consumed
         */
        virtual bool consumerCounterValue(std::uint64_t timestamp,
                                          ApcCounterKeyAndCoreNumber keyAndCore,
                                          std::uint32_t counterValue) = 0;
    };

}
