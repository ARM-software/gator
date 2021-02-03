/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include <cstdint>

/**
 * Builds a frame of FrameType::BLOCK_COUNTER
 */
class IBlockCounterFrameBuilder {

public:
    virtual ~IBlockCounterFrameBuilder() = default;

    /**
     * Sets the timestamp for the following event counts
     * and resets TID to 0
     * @return true if it could be written
     */
    // TODO: rename to setTimestamp
    virtual bool eventHeader(uint64_t curr_time) = 0;

    /**
     * sets the current core, initial is 0
     * @return true if it could be written
     */
    // TODO: rename to setCore
    virtual bool eventCore(int core) = 0;

    /**
     * sets the current TID, initial is 0
     * @return true if it could be written
     */
    // TODO: rename to setTid
    virtual bool eventTid(int tid) = 0;

    /**
     * add a 64 bit counter value to the frame for the current core/TID
     * @return true if it could be written
     */
    virtual bool event64(int key, int64_t value) = 0;

    /**
     * commits the currently built up frame if needed
     * @return true if the current frame was committed (which resets core/tid/timestamp)
     */
    // TODO: rename to commitIfNeeded
    virtual bool check(uint64_t time) = 0;

    /** force commit/flush if there is any data. used at end of capture */
    virtual bool flush() = 0;
};
