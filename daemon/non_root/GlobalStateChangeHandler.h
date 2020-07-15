/* Copyright (C) 2017-2020 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_GLOBALSTATECHANGEHANDLER_H
#define INCLUDE_NON_ROOT_GLOBALSTATECHANGEHANDLER_H

#include "non_root/GlobalCounter.h"

#include <map>

class IBlockCounterMessageConsumer;

namespace non_root {
    class GlobalStateChangeHandler {
    public:
        GlobalStateChangeHandler(IBlockCounterMessageConsumer & outputBuffer,
                                 const std::map<NonRootCounter, int> & enabledCounters);

        void absoluteCounter(unsigned long long timestampNS,
                             unsigned long core,
                             AbsoluteGlobalCounter id,
                             unsigned long long value);

        void absoluteCounter(unsigned long long timestampNS, AbsoluteGlobalCounter id, unsigned long long value);

        void deltaCounter(unsigned long long timestampNS,
                          unsigned long core,
                          DeltaGlobalCounter id,
                          unsigned long long delta);

        void deltaCounter(unsigned long long timestampNS, DeltaGlobalCounter id, unsigned long long delta);

    private:
        IBlockCounterMessageConsumer & outputBuffer;
        const std::map<NonRootCounter, int> & enabledCounters;
    };
}

#endif /* INCLUDE_NON_ROOT_GLOBALSTATECHANGEHANDLER_H */
