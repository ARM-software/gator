/**
 * Copyright (C) Arm Limited 2013-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PERFDRIVER_CONFIGURATION_H
#define PERFDRIVER_CONFIGURATION_H

#include <map>
#include <memory>
#include <vector>

#include "linux/perf/PerfConfig.h"
#include "PmuXML.h"
#include "lib/Span.h"

class GatorCpu;
class UncorePmu;

struct PerfCpu
{
    GatorCpu gator_cpu;
    int pmu_type;
};

struct PerfUncore
{
    UncorePmu uncore_pmu;
    int pmu_type;
};

/**
 * Contains the detected parameters of perf
 */
struct PerfDriverConfiguration
{
    std::vector<PerfCpu> cpus {};
    std::vector<PerfUncore> uncores {};
    std::map<int, int> cpuNumberToSpeType {};
    PerfConfig config {false, false, false, false, false, false, false, false, false, false, false, false, false, false};

    static std::unique_ptr<PerfDriverConfiguration> detect(bool systemWide, lib::Span<const int> cpuIds, const PmuXML & pmuXml);

    static constexpr int UNKNOWN_CPUID = 0xfffff;
};

#endif // PERFDRIVER_CONFIGURATION_H
