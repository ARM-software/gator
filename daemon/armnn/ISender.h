/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include <vector>
#include <cstdint>

namespace armnn
{
    class ISender
    {
    public:
        virtual ~ISender() {}
        virtual bool send(std::vector<std::uint8_t> && data) = 0;
        virtual void stopSending() = 0;
    };
}