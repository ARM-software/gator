/* Copyright (C) 2018-2024 by Arm Limited. All rights reserved. */

#ifndef CONFIGURATION_H_
#define CONFIGURATION_H_

#include "EventCode.h"

#include <set>
#include <string>

enum SampleRate { high = 10007, normal = 1009, low = 101, none = 0, invalid = -1 };

enum class CaptureOperationMode {
    system_wide = 0,
    application_inherit = 1,
    application_no_inherit = 2,
    application_poll = 3,
    application_experimental_patch = 4,
};

[[nodiscard]] constexpr bool isCaptureOperationModeSystemWide(CaptureOperationMode mode)
{
    switch (mode) {

        case CaptureOperationMode::system_wide:
            return true;
        case CaptureOperationMode::application_inherit:
        case CaptureOperationMode::application_no_inherit:
        case CaptureOperationMode::application_poll:
        case CaptureOperationMode::application_experimental_patch:
        default:
            return false;
    }
}

[[nodiscard]] constexpr bool isCaptureOperationModeSupportingCounterGroups(CaptureOperationMode mode)
{
    switch (mode) {

        case CaptureOperationMode::system_wide:
        case CaptureOperationMode::application_no_inherit:
        case CaptureOperationMode::application_poll:
        case CaptureOperationMode::application_experimental_patch:
            return true;
        case CaptureOperationMode::application_inherit:
        default:
            return false;
    }
}

[[nodiscard]] constexpr bool isCaptureOperationModeSupportingMetrics(CaptureOperationMode mode)
{
    return isCaptureOperationModeSupportingCounterGroups(mode);
}

[[nodiscard]] constexpr bool isCaptureOperationModeSupportingUsesInherit(CaptureOperationMode mode)
{
    switch (mode) {

        case CaptureOperationMode::application_inherit:
        case CaptureOperationMode::application_experimental_patch:
            return true;
        case CaptureOperationMode::system_wide:
        case CaptureOperationMode::application_no_inherit:
        case CaptureOperationMode::application_poll:
        default:
            return false;
    }
}

enum class SpeOps {
    LOAD,  //load
    STORE, //store
    BRANCH //branch
};

struct SpeConfiguration {
    std::string id {};
    uint64_t event_filter_mask {}; // if 0 filtering is disabled, else equals PMSEVFR_EL1 (ref doc).
    std::set<SpeOps> ops {};
    int min_latency = 0;
};

inline bool operator==(const SpeConfiguration & lhs, const SpeConfiguration & rhs)
{
    return lhs.id == rhs.id;
}

inline bool operator<(const SpeConfiguration & lhs, const SpeConfiguration & rhs)
{
    return lhs.id < rhs.id;
}

namespace std {
    template<>
    struct hash<SpeConfiguration> {
        using argument_type = SpeConfiguration;
        using result_type = std::size_t;
        result_type operator()(const argument_type & speConfiguration) const noexcept
        {
            return std::hash<std::string> {}(speConfiguration.id);
        }
    };
}

struct CounterConfiguration {
    std::string counterName {};
    EventCode event {};
    int count = 0;
    int cores = 0;
};

inline bool operator==(const CounterConfiguration & lhs, const CounterConfiguration & rhs)
{
    return lhs.counterName == rhs.counterName;
}

inline bool operator<(const CounterConfiguration & lhs, const CounterConfiguration & rhs)
{
    return lhs.counterName < rhs.counterName;
}

namespace std {
    template<>
    struct hash<CounterConfiguration> {
        using argument_type = SpeConfiguration;
        using result_type = std::size_t;
        result_type operator()(const argument_type & counterConfiguration) const noexcept
        {
            return std::hash<std::string> {}(counterConfiguration.id);
        }
    };
}

struct TemplateConfiguration {
    std::string raw {};
};

#endif /* CONFIGURATION_H_ */
