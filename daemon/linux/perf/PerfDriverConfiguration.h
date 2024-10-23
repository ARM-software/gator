/* Copyright (C) 2013-2024 by Arm Limited. All rights reserved. */

#ifndef PERFDRIVER_CONFIGURATION_H
#define PERFDRIVER_CONFIGURATION_H

#include "Configuration.h"
#include "lib/Span.h"
#include "lib/midr.h"
#include "linux/perf/PerfConfig.h"
#include "linux/smmu_identifier.h"
#include "xml/PmuXML.h"

#include <map>
#include <memory>
#include <vector>

class GatorCpu;
class UncorePmu;

struct PerfCpu {
    GatorCpu gator_cpu;
    int pmu_type;

    PerfCpu(GatorCpu gator_cpu, int pmu_type) : gator_cpu(std::move(gator_cpu)), pmu_type(pmu_type) {}
};

struct PerfUncore {
    UncorePmu uncore_pmu;
    int pmu_type;

    PerfUncore(UncorePmu uncore_pmu, int pmu_type) : uncore_pmu(std::move(uncore_pmu)), pmu_type(pmu_type) {}
};

/**
 * Contains the detected parameters of perf
 */
struct PerfDriverConfiguration {
    std::vector<PerfCpu> cpus;
    std::vector<PerfUncore> uncores;
    std::map<int, int> cpuNumberToSpeType;
    PerfConfig config {};

    static std::unique_ptr<PerfDriverConfiguration> detect(
        CaptureOperationMode captureOperationMode,
        const char * tracefsEventsPath,
        lib::Span<const cpu_utils::midr_t> midrs,
        const gator::smmuv3::default_identifiers_t & smmu_identifiers,
        const PmuXML & pmuXml);

    static constexpr const char * ARMV82_SPE = "armv8.2_spe";
};

#endif // PERFDRIVER_CONFIGURATION_H
