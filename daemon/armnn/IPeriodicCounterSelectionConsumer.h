/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#ifndef ARMNN_IPERIODICCOUNTERSELECTIONCONSUMER_H_
#define ARMNN_IPERIODICCOUNTERSELECTIONCONSUMER_H_

#include <cstdint>
#include <set>

namespace armnn {
    class IPeriodicCounterSelectionConsumer {

    public:
        virtual ~IPeriodicCounterSelectionConsumer() = default;
        /**
         *TODO : returns true, if the packet is successfully consumed,
         */
        virtual bool onPeriodicCounterSelection(std::uint32_t period, std::set<std::uint16_t> uids) = 0;
    };
}

#endif /* ARMNN_IPERIODICCOUNTERSELECTIONCONSUMER_H_ */
