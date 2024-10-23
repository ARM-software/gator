/* Copyright (C) 2020-2024 by Arm Limited. All rights reserved. */

#pragma once

#include <cinttypes>
#include <cstdint>

class EventCode {
public:
    static constexpr std::uint64_t INVALID_EVENT_CODE_VALUE = ~0ULL;

    constexpr EventCode() noexcept : value(INVALID_EVENT_CODE_VALUE) {}
    constexpr explicit EventCode(unsigned long long value) noexcept : value(value) {}

    [[nodiscard]] constexpr bool isValid() const noexcept { return value != INVALID_EVENT_CODE_VALUE; }
    [[nodiscard]] constexpr std::uint64_t asU64() const noexcept { return value; }
    [[nodiscard]] constexpr std::int32_t asI32() const noexcept { return value; }

    constexpr bool operator==(const EventCode & that) const noexcept { return (value == that.value); }
    constexpr bool operator<(const EventCode & that) const noexcept { return value < that.value; }

private:
    std::uint64_t value;
};

#define PRIxEventCode PRIx64
#define PRIuEventCode PRIu64
