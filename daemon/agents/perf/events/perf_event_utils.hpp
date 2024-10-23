/* Copyright (C) 2022-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "agents/perf/events/types.hpp"
#include "k/perf_event.h"
#include "lib/midr.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace agents::perf {

    /**
     * A class that can be used to stringify various aspects of a perf event
     */
    class perf_event_printer_t {
    public:
        constexpr perf_event_printer_t(std::map<cpu_utils::cpuid_t, std::string> const & cpuid_to_core_name,
                                       std::vector<cpu_utils::midr_t> const & per_core_midrs,
                                       std::map<std::uint32_t, std::string> const & perf_pmu_type_to_name)
            : per_core_midrs(per_core_midrs),
              cpuid_to_core_name(cpuid_to_core_name),
              perf_pmu_type_to_name(perf_pmu_type_to_name)
        {
        }

        /**
         * To map the type field for some event to a string name for the associated PMU
         *
         * @param type The PMU type code
         * @param core_no The core number associated with the event
         * @return The name of the PMU
         */
        [[nodiscard]] char const * map_attr_type(std::uint32_t type, core_no_t core_no);

        /**
         * Format a perf_event_attr to a string (for logging, errors)
         *
         * @param attr The attr to log
         * @param core_no The core no associated with the event
         * @param indentation For indenting each element
         * @param separator For separating each element
         * @return The attr string
         */
        [[nodiscard]] std::string perf_attr_to_string(perf_event_attr const & attr,
                                                      core_no_t core_no,
                                                      char const * indentation,
                                                      char const * separator);

    private:
        std::vector<cpu_utils::midr_t> const & per_core_midrs;
        std::map<cpu_utils::cpuid_t, std::string> const & cpuid_to_core_name;
        std::map<std::uint32_t, std::string> const & perf_pmu_type_to_name;

        [[nodiscard]] char const * map_core_cluster_name(core_no_t core_no);
        [[nodiscard]] char const * map_custom_pmu_type(std::uint32_t type, core_no_t core_no);
    };
}
