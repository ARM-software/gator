/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "SessionData.h"

#include <memory>
#include <system_error>

namespace gator::capture {

    class CaptureEnvironment {
    public:
        CaptureEnvironment() = default;
        virtual ~CaptureEnvironment() = default;

        CaptureEnvironment(const CaptureEnvironment &) = delete;
        CaptureEnvironment & operator=(const CaptureEnvironment &) = delete;
    };

    class LinuxEnvironmentConfig : public CaptureEnvironment {
    public:
        LinuxEnvironmentConfig(SessionData & sessionData) noexcept;

        virtual ~LinuxEnvironmentConfig() noexcept;
    };

    std::unique_ptr<CaptureEnvironment> prepareCaptureEnvironment(SessionData & sessionData);

}

#endif
