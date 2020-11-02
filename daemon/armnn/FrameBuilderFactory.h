/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include "CommitTimeChecker.h"
#include "armnn/IFrameBuilderFactory.h"

#include <memory>

class IRawFrameBuilder;

namespace armnn {

    /**
     * Creates builders for different frames
     *
     * At most one frame can be current at one time
     */
    class FrameBuilderFactory : public IFrameBuilderFactory {
    public:
        FrameBuilderFactory(IRawFrameBuilder & rawBuilder, std::uint64_t commitRate)
            : rawBuilder(rawBuilder), flushIsNeeded(std::make_shared<CommitTimeChecker>(commitRate))
        {
        }

        std::unique_ptr<IBlockCounterMessageConsumer> createBlockCounterFrame() override;

        void createExternalFrame(std::uint32_t fd, lib::Span<const std::uint8_t> data) override;

    private:
        IRawFrameBuilder & rawBuilder;
        std::shared_ptr<CommitTimeChecker> flushIsNeeded;
    };
}
