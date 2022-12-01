/* Copyright (C) 2022 by Arm Limited. All rights reserved. */
#pragma once

#include "lib/Assert.h"
#include "linux/smmu_identifier.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

struct PerfDriverConfiguration;
struct PmuXML;

namespace gator::smmuv3 {

    /**
     * Attempts to match a sysfs PMU device against the list parsed from the PMU XML file.
     * If a suitable match is found an UncorePmu is added to the PerfDriverConfiguration.
     *
     * @param pmu_xml The parsed PMU XML object.
     * @param default_identifiers The identifiers to use if the PMU can't be identified from the sysfs entires.
     * @param config The driver configuration to update if a valid PMU is found.
     * @param pmu_name The device name from sysfs (/sys/bus/event_source/devices/<pmu_name>
     * @return Returns true if the PMU was successfully identified, otherwise false.
     */
    [[nodiscard]] bool detect_smmuv3_pmus(const PmuXML & pmu_xml,
                                          const default_identifiers_t & default_identifiers,
                                          PerfDriverConfiguration & config,
                                          std::string_view pmu_name);
}
