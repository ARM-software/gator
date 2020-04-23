/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#ifndef ARMNN_IPERJOBCOUNTERSELECTIONCONSUMER_H_
#define ARMNN_IPERJOBCOUNTERSELECTIONCONSUMER_H_
#include <cstdint>
#include <set>

namespace armnn {
    class IPerJobCounterSelectionConsumer {
    public:
        virtual ~IPerJobCounterSelectionConsumer() = default;
        virtual bool onPerJobCounterSelection(std::uint64_t objectId, std::set<std::uint16_t> uids) = 0;
    };
}

#endif /* ARMNN_IPERJOBCOUNTERSELECTIONCONSUMER_H_ */
