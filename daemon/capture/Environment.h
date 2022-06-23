/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "SessionData.h"

#include <memory>
#include <system_error>

namespace capture {

    enum class OsType { Linux, Android };

    class CaptureEnvironment {
    public:
        CaptureEnvironment() = default;
        virtual ~CaptureEnvironment() = default;

        CaptureEnvironment(const CaptureEnvironment &) = delete;
        CaptureEnvironment & operator=(const CaptureEnvironment &) = delete;

        virtual void postInit(SessionData & sessionData) = 0;
    };

    class LinuxEnvironmentConfig : public CaptureEnvironment {
    public:
        LinuxEnvironmentConfig() noexcept;

        ~LinuxEnvironmentConfig() noexcept override;

        void postInit(SessionData & sessionData) override;
    };

    OsType detectOs();
    std::unique_ptr<CaptureEnvironment> prepareCaptureEnvironment();
}

#endif
