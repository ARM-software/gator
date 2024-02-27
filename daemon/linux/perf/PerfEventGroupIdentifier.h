/* Copyright (C) 2018-2024 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_PERF_PERF_EVENT_GROUP_IDENTIFIER_H
#define INCLUDE_LINUX_PERF_PERF_EVENT_GROUP_IDENTIFIER_H

#include <cstdint>
#include <cstring>
#include <map>
#include <string>

class GatorCpu;
class UncorePmu;

/// Note: these are not (necessarily) perf_event_open groups they are
/// gatord specific grouping of events, some of which will be used as a
/// perf_event_open group, some are just a collection of separate
/// perf_event_open group leaders
class PerfEventGroupIdentifier {
public:
    enum class Type {
        PER_CLUSTER_CPU_PINNED,
        PER_CLUSTER_CPU_MUXED,
        UNCORE_PMU,
        SPECIFIC_CPU,
        GLOBAL,
        SPE,
    };

    /** Constructor, for global events on all CPUs */
    PerfEventGroupIdentifier() = default;

    /** Constructor, for each CPU PMU in a specific cluster */
    PerfEventGroupIdentifier(const GatorCpu & cluster, std::uint32_t groupNo = 0);

    /** Constructor, for a given UncorePmu */
    PerfEventGroupIdentifier(const UncorePmu & pmu);

    /** Constructor, for global events associated with a specific core */
    PerfEventGroupIdentifier(int cpuNumber);

    /** Constructor, for SPE events that have a core specific type */
    PerfEventGroupIdentifier(const std::map<int, int> & cpuToTypeMap);

    /** Equality operator, are they the same group? */
    [[nodiscard]] bool operator==(const PerfEventGroupIdentifier & that) const
    {
        return (cluster == that.cluster) && (pmu == that.pmu) && (cpuNumber == that.cpuNumber)
            && (cpuNumberToType == that.cpuNumberToType);
    }

    /** Inequality operator, are they not the same group? */
    [[nodiscard]] bool operator!=(const PerfEventGroupIdentifier & that) const { return !(*this == that); }

    /** Less operator, to allow use in std::map as key */
    [[nodiscard]] bool operator<(const PerfEventGroupIdentifier & that) const;

    /** Convert to string for logging purposes */
    [[nodiscard]] operator std::string() const;

    /* Accessors */

    [[nodiscard]] const GatorCpu * getCluster() const { return cluster; }

    [[nodiscard]] const UncorePmu * getUncorePmu() const { return pmu; }

    [[nodiscard]] const std::map<int, int> * getSpeTypeMap() const { return cpuNumberToType; }

    [[nodiscard]] int getCpuNumber() const { return (cluster != nullptr ? invalid_cpu_number : cpuNumber); }

    [[nodiscard]] Type getType() const
    {
        if (cluster != nullptr) {
            return (groupNo > 0 ? Type::PER_CLUSTER_CPU_MUXED : Type::PER_CLUSTER_CPU_PINNED);
        }
        if (pmu != nullptr) {
            return Type::UNCORE_PMU;
        }
        if (cpuNumberToType != nullptr) {
            return Type::SPE;
        }
        if (cpuNumber >= 0) {
            return Type::SPECIFIC_CPU;
        }
        return Type::GLOBAL;
    }

    [[nodiscard]] std::uint32_t getClusterGroupNo() const { return (cluster != nullptr ? groupNo : 0); }

private:
    static constexpr int invalid_cpu_number = -1;

    const GatorCpu * const cluster = nullptr;
    const UncorePmu * const pmu = nullptr;
    const union {
        int cpuNumber = invalid_cpu_number;
        std::uint32_t groupNo;
    };
    const std::map<int, int> * const cpuNumberToType = nullptr;
};

#endif /* INCLUDE_LINUX_PERF_PERF_EVENT_GROUP_IDENTIFIER_H */
