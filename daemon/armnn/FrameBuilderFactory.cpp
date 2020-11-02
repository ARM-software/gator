/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#include "armnn/FrameBuilderFactory.h"

#include "BlockCounterFrameBuilder.h"
#include "BlockCounterMessageConsumer.h"
#include "BufferUtils.h"
#include "IRawFrameBuilder.h"

namespace armnn {

    std::unique_ptr<IBlockCounterMessageConsumer> FrameBuilderFactory::createBlockCounterFrame()
    {
        return std::unique_ptr<IBlockCounterMessageConsumer> {
            new BlockCounterMessageConsumer {std::make_shared<BlockCounterFrameBuilder>(rawBuilder, flushIsNeeded)}};
    };

    void FrameBuilderFactory::createExternalFrame(std::uint32_t fd, lib::Span<const std::uint8_t> data)
    {
        rawBuilder.waitForSpace(IRawFrameBuilder::MAX_FRAME_HEADER_SIZE + buffer_utils::MAXSIZE_PACK32 + data.size());
        rawBuilder.beginFrame(FrameType::EXTERNAL);
        rawBuilder.packInt(fd);
        rawBuilder.writeBytes(data.data, data.size());
        rawBuilder.endFrame();
    }
}
