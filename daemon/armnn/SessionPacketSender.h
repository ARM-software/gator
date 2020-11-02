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

        // ISessionPacketSender:
        bool requestActivateCounterSelection(CaptureMode mode,
                                             std::uint32_t period,
                                             const std::set<std::uint16_t> & eventUids) override;
        bool requestDisableCounterSelection() override;
        bool requestActivateTimelineReporting() override;
        bool requestDeactivateTimelineReporting() override;

    private:
        std::unique_ptr<IEncoder> mEncoder;
        std::unique_ptr<ISender> mSender;
    };
}
