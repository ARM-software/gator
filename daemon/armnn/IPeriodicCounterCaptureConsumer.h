/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#ifndef ARMNN_IPERIODICCOUNTERCAPTURECONSUMER_H_
#define ARMNN_IPERIODICCOUNTERCAPTURECONSUMER_H_

#include <cstdint>
#include <map>

namespace armnn {
    class IPeriodicCounterCaptureConsumer {

    public:
        virtual ~IPeriodicCounterCaptureConsumer() = default;
        virtual bool onPeriodicCounterCapture(std::uint64_t timeStamp,
                                              std::map<std::uint16_t, std::uint32_t> counterIndexValues) = 0;
    };
}

#endif /* ARMNN_IPERIODICCOUNTERCAPTURECONSUMER_H_ */
