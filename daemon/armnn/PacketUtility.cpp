/*  Copyright (C) 2020 by Arm Limited. All rights reserved. */

#include "armnn/PacketUtility.h"

#include <cstdint>
namespace armnn {
    /**
     * Get bits from a given number, between msb and lsb , ([msb, lsb])
     */
    std::uint32_t getBits(const std::uint32_t number, int lsb, int msb)
    {
        int range = (msb - lsb) + 1;
        return (((1 << range) - 1) & (number >> (lsb)));
    }
}
