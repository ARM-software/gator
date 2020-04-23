/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include "CaptureMode.h"

#include <cstdint>
#include <set>

namespace armnn {
    class ISessionPacketSender {
    public:
        virtual ~ISessionPacketSender() = default;
        /**
         * Send a counter selection packet to ArmNN to request the activation of counters
         *
         * @param mode
         * @param period
         * @param eventUids The UIDs of events, which could be empty if nothing was selected in this session
         * @return True if request was successful, false if not
         */
        virtual bool requestActivateCounterSelection(CaptureMode mode, std::uint32_t period,
                                                     const std::set<std::uint16_t> &eventUids) = 0;
        /**
         * Send a counter selection packet, disabling all counters
         *
         * @return True if request was successful, false if not
         */
        virtual bool requestDisableCounterSelection() = 0;
    };
}