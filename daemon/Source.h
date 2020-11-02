/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#pragma once

#include "lib/Optional.h"

#include <cstdint>
#include <functional>

class ISender;

class Source {
public:
    virtual ~Source() = default;

    virtual void run(std::uint64_t monotonicStart, std::function<void()> endSession) = 0;
    virtual void interrupt() = 0;

    /**
     * @return true if done, nothing more to write
     */
    virtual bool write(ISender & sender) = 0;
};

class PrimarySource : public Source {
public:
    /**
     * Send the summary message
     *
     * @return monotonic start or empty on failure
     */
    virtual lib::Optional<std::uint64_t> sendSummary() = 0;
};
