// Copyright (C) 2025 by Arm Limited (or its affiliates). All rights reserved.
#pragma once

#include "ICpuInfo.h"
#include "capture/Environment.h"
#include "lib/Utils.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

enum class tri_bool_t { yes, no, unknown };

inline std::ostream & operator<<(std::ostream & out, tri_bool_t v)
{
    switch (v) {
        case tri_bool_t::yes:
            return out << "yes";
        case tri_bool_t::no:
            return out << "no";
        case tri_bool_t::unknown:
            return out << "unknown";
    }
    return out;
}

struct advice_message_t {
    enum class severity_t : std::uint8_t {
        info,
        warning,
        error,
    };

    std::string message;
    severity_t severity;
};

struct setup_warnings_t {
    capture::OsType os_type = capture::OsType::Linux;
    lib::kernel_version_no_t kernel_version = 0;
    tri_bool_t supports_counter_strobing = tri_bool_t::unknown;
    tri_bool_t supports_event_inherit = tri_bool_t::unknown;
    std::unordered_map<cpu_utils::cpuid_t, std::size_t> number_of_counters_by_cpu;
    std::vector<advice_message_t> advice_messages;

    void add_warning(std::string message)
    {
        advice_messages.push_back({
            .message = std::move(message),
            .severity = advice_message_t::severity_t::warning,
        });
    }

    void add_error(std::string message)
    {
        advice_messages.push_back({
            .message = std::move(message),
            .severity = advice_message_t::severity_t::error,
        });
    }

    [[nodiscard]] const std::vector<advice_message_t> & get_advice_messages() const { return advice_messages; }
};
