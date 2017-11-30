/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#include "non_root/GlobalStateChangeHandler.h"
#include "Buffer.h"

#include <algorithm>

namespace non_root
{
    GlobalStateChangeHandler::GlobalStateChangeHandler(Buffer & outputBuffer_,
                                                       const std::map<NonRootCounter, int> & enabledCounters_)
            : outputBuffer(outputBuffer_),
              enabledCounters(enabledCounters_)
    {
    }

    void GlobalStateChangeHandler::absoluteCounter(unsigned long long timestampNS, unsigned long core,
                                                   AbsoluteGlobalCounter id, unsigned long long value)
    {
        const auto it = enabledCounters.find(NonRootCounter(id));

        if (it != enabledCounters.end()) {
            outputBuffer.counterMessage(timestampNS, std::max<int>(core, 0), it->second, value);
        }
    }

    void GlobalStateChangeHandler::absoluteCounter(unsigned long long timestampNS, AbsoluteGlobalCounter id,
                                                   unsigned long long value)
    {
        const auto it = enabledCounters.find(NonRootCounter(id));

        if (it != enabledCounters.end()) {
            outputBuffer.counterMessage(timestampNS, 0, it->second, value);
        }
    }

    void GlobalStateChangeHandler::deltaCounter(unsigned long long timestampNS, unsigned long core,
                                                DeltaGlobalCounter id, unsigned long long delta)
    {
        const auto it = enabledCounters.find(NonRootCounter(id));

        if (it != enabledCounters.end()) {
            outputBuffer.counterMessage(timestampNS, std::max<int>(core, 0), it->second, delta);
        }
    }

    void GlobalStateChangeHandler::deltaCounter(unsigned long long timestampNS, DeltaGlobalCounter id,
                                                unsigned long long delta)
    {
        const auto it = enabledCounters.find(NonRootCounter(id));

        if (it != enabledCounters.end()) {
            outputBuffer.counterMessage(timestampNS, 0, it->second, delta);
        }
    }
}

