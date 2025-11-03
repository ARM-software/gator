/* Copyright (C) 2018-2025 by Arm Limited (or its affiliates). All rights reserved. */

#ifndef CONFIGURATION_H_
#define CONFIGURATION_H_

#include "EventCode.h"
#include "linux/perf/PerfEventGroupIdentifier.h"

#include <set>
#include <string>

enum SampleRate { high = 10007, normal = 1009, normal_x2 = 2003, low = 101, none = 0, invalid = -1 };

enum class CaptureOperationMode {
    system_wide = 0,
    application_default = 1,
    application_inherit = 2,
    application_no_inherit = 3,
    application_poll = 4,
    application_experimental_patch = 5,
};

[[nodiscard]] constexpr bool isCaptureOperationModeSystemWide(CaptureOperationMode mode)
{
    switch (mode) {

        case CaptureOperationMode::system_wide:
            return true;
        case CaptureOperationMode::application_default:
        case CaptureOperationMode::application_inherit:
        case CaptureOperationMode::application_no_inherit:
        case CaptureOperationMode::application_poll:
        case CaptureOperationMode::application_experimental_patch:
        default:
            return false;
    }
}

[[nodiscard]] constexpr bool isCaptureOperationModeSupportingCounterGroups(CaptureOperationMode mode,
                                                                           bool supports_inherit_sample_read)
{
    switch (mode) {
        case CaptureOperationMode::system_wide:
        case CaptureOperationMode::application_no_inherit:
        case CaptureOperationMode::application_poll:
        case CaptureOperationMode::application_experimental_patch:
            return true;
        case CaptureOperationMode::application_default:
            return supports_inherit_sample_read;
        case CaptureOperationMode::application_inherit:
        default:
            return false;
    }
}

[[nodiscard]] constexpr bool isCaptureOperationModeSupportingUsesInherit(CaptureOperationMode mode)
{
    switch (mode) {
        case CaptureOperationMode::application_default:
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
    std::string id;
    uint64_t event_filter_mask {}; // if 0 filtering is disabled, else equals PMSEVFR_EL1 (ref doc).
    std::set<SpeOps> ops;
    int min_latency = 0;
    bool inverse_event_filter_mask {false}; // When this is set event_filter_mask will be set for PMSNEVFR_EL1
    static constexpr std::string_view workflow_spe {"workflow_spe"};

    [[nodiscard]] bool applies_to_counter(std::string_view const counter_name,
                                          PerfEventGroupIdentifier const & pegi) const
    {
        return id == counter_name ||  //
               (id == workflow_spe && //
                pegi.getType() == PerfEventGroupIdentifier::Type::SPE);
    }
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
    std::string counterName;
    EventCode event;
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
    std::string raw;
};

enum class GPUTimelineEnablement {
    disable,   ///< Disable GPU Timeline data collection
    enable,    ///< Enable GPU Timeline data collection if counter
               ///< MaliTimeline_Perfetto is present - error otherwise
    automatic, ///< Enable GPU Timeline data collection if counter
               ///< MaliTimeline_Perfetto is present - do nothing otherwise
};

enum class MetricSamplingMode {
    automatic,
    ebs,
    strobing,
};

#endif /* CONFIGURATION_H_ */
