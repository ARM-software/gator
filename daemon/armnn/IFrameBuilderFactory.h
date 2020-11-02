/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include "IBlockCounterMessageConsumer.h"
#include "lib/Span.h"

#include <memory>

namespace armnn {
    /**
     * Creates builders for different frames
     *
     * At most one frame can be current at one time
     */
    class IFrameBuilderFactory {
    public:
        virtual ~IFrameBuilderFactory() = default;

        /**
         * Starts a BLOCK_COUNTER frame
         * Will be finished when returned consumer is destroyed
         */
        virtual std::unique_ptr<IBlockCounterMessageConsumer> createBlockCounterFrame() = 0;

        /**
         * Starts and finishes an EXTERNAL frame
         *
         * @param fd the file descriptor (or ID) for this data
         * @param data opaque external data blob
         */
        virtual void createExternalFrame(std::uint32_t fd, lib::Span<const std::uint8_t> data) = 0;
    };
}
