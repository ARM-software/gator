/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#ifndef ARMNN_IPACKETCONSUMER_H_
#define ARMNN_IPACKETCONSUMER_H_


#include "ICounterDirectoryConsumer.h"
#include "IPeriodicCounterSelectionConsumer.h"
#include "IPerJobCounterSelectionConsumer.h"
#include "IPeriodicCounterCaptureConsumer.h"
#include "IPerJobCounterCaptureConsumer.h"

namespace armnn
{

    class IPacketConsumer : public ICounterDirectoryConsumer,
                                 public IPeriodicCounterSelectionConsumer,
                                 public IPerJobCounterSelectionConsumer,
                                 public IPeriodicCounterCaptureConsumer,
                                 public IPerJobCounterCaptureConsumer
    {
    public:
        virtual ~IPacketConsumer() = default;
    };

}

#endif /* ARMNN_IARMNNPACKETCONSUMER_H_ */
