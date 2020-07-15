/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include <cstdint>
#include <vector>

namespace armnn {
    class ISender {
    public:
        virtual ~ISender() = default;
        virtual bool send(std::vector<std::uint8_t> && data) = 0;
    };
}