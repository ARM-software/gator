/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#ifndef ARMNN_IPERJOBCOUNTERCAPTURECONSUMER_H_
#define ARMNN_IPERJOBCOUNTERCAPTURECONSUMER_H_

#include <cstdint>
#include <map>

namespace armnn {
    class IPerJobCounterCaptureConsumer {

    public:
        virtual ~IPerJobCounterCaptureConsumer() = default;
        virtual bool onPerJobCounterCapture(bool isPre,
                                            std::uint64_t timeStamp,
                                            std::uint64_t objectRef,
                                            std::map<std::uint16_t, std::uint32_t> counterIndexValues) = 0;
    };
}

#endif /* ARMNN_IPERJOBCOUNTERCAPTURECONSUMER_H_ */
