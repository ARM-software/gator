/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#include "armnn/SessionPacketSender.h"

namespace armnn {
    SessionPacketSender::SessionPacketSender(std::unique_ptr<ISender> sender, std::unique_ptr<IEncoder> encoder)
        : mEncoder {std::move(encoder)}, mSender {std::move(sender)}
    {
    }

    bool SessionPacketSender::requestActivateCounterSelection(CaptureMode mode,
                                                              std::uint32_t period,
                                                              const std::set<std::uint16_t> & eventUids)
    {
        std::vector<std::uint8_t> packet;
        if (mode == CaptureMode::PERIOD_CAPTURE) {
            packet = mEncoder->encodePeriodicCounterSelectionRequest(period, eventUids);
        }
        else {
            packet = mEncoder->encodePerJobCounterSelectionRequest(period, eventUids);
        }

        return mSender->send(std::move(packet));
    }

    bool SessionPacketSender::requestDisableCounterSelection()
    {
        // Send empty set to disable counter selection on periodic and per job
        std::vector<std::uint8_t> periodicPacketDisable = mEncoder->encodePeriodicCounterSelectionRequest(0, {});
        std::vector<std::uint8_t> perJobPacketDisable = mEncoder->encodePerJobCounterSelectionRequest(0, {});
        bool periodicSuccess = mSender->send(std::move(periodicPacketDisable));
        bool perjobSuccess = mSender->send(std::move(perJobPacketDisable));
        return periodicSuccess && perjobSuccess;
    }

    bool SessionPacketSender::requestActivateTimelineReporting()
    {
        std::vector<std::uint8_t> activateTimelinePacket = mEncoder->encodeActivateTimelineReportingPacket();
        return mSender->send(std::move(activateTimelinePacket));
    }

    bool SessionPacketSender::requestDeactivateTimelineReporting()
    {
        std::vector<std::uint8_t> deactivateTimelinePacket = mEncoder->encodeDeactivateTimelineReportingPacket();
        return mSender->send(std::move(deactivateTimelinePacket));
    }
}
