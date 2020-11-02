/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#ifndef ARMNN_IPACKETCONSUMER_H_
#define ARMNN_IPACKETCONSUMER_H_

#include "armnn/ICounterDirectoryConsumer.h"
#include "armnn/IPerJobCounterCaptureConsumer.h"
#include "armnn/IPerJobCounterSelectionConsumer.h"
#include "armnn/IPeriodicCounterCaptureConsumer.h"
#include "armnn/IPeriodicCounterSelectionConsumer.h"

namespace armnn {

    class IPacketConsumer : public ICounterDirectoryConsumer,
                            public IPeriodicCounterSelectionConsumer,
                            public IPerJobCounterSelectionConsumer,
                            public IPeriodicCounterCaptureConsumer,
                            public IPerJobCounterCaptureConsumer {
    public:
        ~IPacketConsumer() override = default;
    };
}

#endif /* ARMNN_IARMNNPACKETCONSUMER_H_ */
