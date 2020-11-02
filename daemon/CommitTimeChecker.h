/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include <cstdint>

class CommitTimeChecker {
public:
    CommitTimeChecker(std::uint64_t commitRate) : commitRate(commitRate), nextCommit(commitRate) {}

    bool operator()(std::uint64_t time, bool force)
    {
        if (force || ((commitRate > 0) && time >= nextCommit)) {
            nextCommit = time + commitRate;
            return true;
        }
        else {
            return false;
        }
    }

private:
    std::uint64_t commitRate;
    std::uint64_t nextCommit;
};
