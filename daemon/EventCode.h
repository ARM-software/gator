/* Copyright (C) 2020-2024 by Arm Limited. All rights reserved. */

#pragma once

#include <cinttypes>
#include <cstdint>
#include <utility>

class EventCode {
public:
    static constexpr std::uint64_t INVALID_EVENT_CODE_VALUE = ~0ULL;

    constexpr EventCode() noexcept : value(INVALID_EVENT_CODE_VALUE) {}
    constexpr explicit EventCode(int value) noexcept : value(value & 0xffffffffULL) {}
    constexpr explicit EventCode(long long value) noexcept : value(value) {}
    constexpr explicit EventCode(unsigned long long value) noexcept : value(value) {}

    constexpr EventCode(const EventCode & that) noexcept = default;
    EventCode & operator=(const EventCode & that) noexcept
    {
        if (this != &that) {
            value = that.value;
        }
        return *this;
    }
    EventCode(EventCode && that) noexcept
        : value(that.value) // FIXME: C++14, make constexpr, using std::exchange(that.value, INVALID_EVENT_CODE_VALUE)
    {
        that.value = INVALID_EVENT_CODE_VALUE;
    }
    EventCode & operator=(EventCode && that) noexcept
    {
        if (this != &that) {
            value = that.value;
            that.value = INVALID_EVENT_CODE_VALUE;
        }
        return *this;
    }

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
