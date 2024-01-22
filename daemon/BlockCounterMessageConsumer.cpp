/* Copyright (C) 2020-2023 by Arm Limited. All rights reserved. */

#include "BlockCounterMessageConsumer.h"

#include "IBlockCounterFrameBuilder.h"

#include <cstdint>

bool BlockCounterMessageConsumer::threadCounterMessage(uint64_t curr_time, int core, int tid, int key, int64_t value)
{
    if ((mLastEventTime != curr_time) || (mLastEventTime == INVALID_LAST_EVENT_TIME)) {
        if (!mBuilder->eventHeader(curr_time)) {
            return false;
        }
        mLastEventTime = curr_time;
        // change of time resets TID
        mLastEventTid = 0;
    }

    if (mLastEventCore != core) {
        if (!mBuilder->eventCore(core)) {
            return false;
        }
        mLastEventCore = core;
    }

    if (mLastEventTid != tid) {
        if (!mBuilder->eventTid(tid)) {
            return false;
        }
        mLastEventTid = tid;
    }

    if (!mBuilder->event64(key, value)) {
        return false;
    }

    if (mBuilder->check(curr_time)) {
        // new frame resets everything
        mLastEventTime = INVALID_LAST_EVENT_TIME;
        mLastEventCore = 0;
        mLastEventTid = 0;
    }

    return true;
}
