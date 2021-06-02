/* Copyright (C) 2020-2021 by Arm Limited. All rights reserved. */

#pragma once

#include <cstdint>

/**
 * Consumes a message from a frame of FrameType::BLOCK_COUNTER
 */
class IBlockCounterMessageConsumer {

public:
    virtual ~IBlockCounterMessageConsumer() = default;

    /**
     * Consumes a counter message with thread information
     * @param curr_time the relative offset of the counter value from the monotonic time of capture started
     * @param core which core the counter value relates to
     * @param tid which thread
     * @param key the counter key
     * @param value the value of the counter at <curr_time>
     * @return true if it could be written
     */
    virtual bool threadCounterMessage(uint64_t curr_time, int core, int tid, int key, int64_t value) = 0;

    /**
     * Consumes a counter message
     * @param curr_time the relative offset of the counter value from the monotonic time of capture started
     * @param core which core the counter value relates to
     * @param key the counter key
     * @param value the value of the counter at <curr_time>
     * @return true if it could be written
     */
    bool counterMessage(uint64_t curr_time, int core, int key, int64_t value)
    {
        return threadCounterMessage(curr_time, core, 0, key, value);
    }
};
