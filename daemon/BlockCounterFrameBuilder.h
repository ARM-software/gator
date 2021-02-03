/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include "CommitTimeChecker.h"
#include "IBlockCounterFrameBuilder.h"

#include <memory>

class IRawFrameBuilder;

/**
 * Builds block counter frames
 *
 * Creates and splits frames as needed
 */
class BlockCounterFrameBuilder : public IBlockCounterFrameBuilder {
public:
    BlockCounterFrameBuilder(IRawFrameBuilder & rawBuilder, std::uint64_t commitRate)
        : rawBuilder(rawBuilder), flushIsNeeded(std::make_shared<CommitTimeChecker>(commitRate))
    {
    }

    BlockCounterFrameBuilder(IRawFrameBuilder & rawBuilder, std::shared_ptr<CommitTimeChecker> checker)
        : rawBuilder(rawBuilder), flushIsNeeded(checker)
    {
    }

    virtual ~BlockCounterFrameBuilder() override;

    virtual bool eventHeader(uint64_t time) override;

    virtual bool eventCore(int core) override;

    virtual bool eventTid(int tid) override;

    virtual bool event64(int key, int64_t value) override;

    virtual bool check(const uint64_t time) override;

    virtual bool flush() override;

private:
    IRawFrameBuilder & rawBuilder;
    std::shared_ptr<CommitTimeChecker> flushIsNeeded;
    bool isFrameStarted = false;

    bool ensureFrameStarted();
    bool endFrame();
    bool checkSpace(const int bytes);
};
