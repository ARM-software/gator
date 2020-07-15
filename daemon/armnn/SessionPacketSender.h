/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include "armnn/IEncoder.h"
#include "armnn/ISender.h"
#include "armnn/ISessionPacketSender.h"

#include <cstdint>
#include <memory>
#include <set>

namespace armnn {
    class SessionPacketSender : public ISessionPacketSender {
    public:
        SessionPacketSender(std::unique_ptr<ISender> sender, std::unique_ptr<IEncoder> encoder);

        SessionPacketSender(const SessionPacketSender &) = delete;
        SessionPacketSender & operator=(const SessionPacketSender &) = delete;

        SessionPacketSender(SessionPacketSender && that) = default;
        SessionPacketSender & operator=(SessionPacketSender && that) = default;

        /**
         * Send a counter selection packet to ArmNN to request the activation of counters
         *
         * @param mode
         * @param period
         * @param eventUids The UIDs of events, which could be empty if nothing was selected in this session
         * @return True if request was successful, false if not
         */
        bool requestActivateCounterSelection(CaptureMode mode,
                                             std::uint32_t period,
                                             const std::set<std::uint16_t> & eventUids) override;
        /**
         * Send a counter selection packet, disabling all counters
         *
         * @return True if request was successful, false if not
         */
        bool requestDisableCounterSelection() override;

    private:
        std::unique_ptr<IEncoder> mEncoder;
        std::unique_ptr<ISender> mSender;
    };
}