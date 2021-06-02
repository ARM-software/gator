/* Copyright (C) 2020-2021 by Arm Limited. All rights reserved. */

#pragma once

#include "IBlockCounterMessageConsumer.h"

#include <memory>
#include <utility>

class IBlockCounterFrameBuilder;

class BlockCounterMessageConsumer : public IBlockCounterMessageConsumer {
public:
    /**
     * Possibly-owning constructor
     */
    BlockCounterMessageConsumer(std::shared_ptr<IBlockCounterFrameBuilder> builder) : mBuilder(std::move(builder)) {}

    /**
     * Non-owning constructor
     * @param builder must outlive this
     */
    BlockCounterMessageConsumer(IBlockCounterFrameBuilder & builder)
        // use the reference in a shared_ptr that aliases an empty shared_ptr
        // so the reference isn't deleted when the shared_ptr is destroyed
        : BlockCounterMessageConsumer({std::shared_ptr<IBlockCounterFrameBuilder> {}, &builder})
    {
    }

    bool threadCounterMessage(uint64_t curr_time, int core, int tid, int key, int64_t value) override;

private:
    static const uint64_t INVALID_LAST_EVENT_TIME = UINT64_MAX;

    std::shared_ptr<IBlockCounterFrameBuilder> mBuilder;
    uint64_t mLastEventTime = INVALID_LAST_EVENT_TIME;
    int mLastEventCore = 0;
    int mLastEventTid = 0;
};
