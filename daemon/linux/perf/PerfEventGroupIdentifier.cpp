/* Copyright (C) 2018-2024 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfEventGroupIdentifier.h"

#include "lib/Format.h"
#include "lib/String.h"
#include "xml/PmuXML.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <map>
#include <string>

PerfEventGroupIdentifier::PerfEventGroupIdentifier(const GatorCpu & cluster, std::uint32_t groupNo)
    : cluster(&cluster), groupNo(groupNo)
{
}

PerfEventGroupIdentifier::PerfEventGroupIdentifier(const UncorePmu & pmu) : pmu(&pmu)
{
}

PerfEventGroupIdentifier::PerfEventGroupIdentifier(int cpuNumber) : cpuNumber(cpuNumber)
{
    assert(cpuNumber >= 0);
}

PerfEventGroupIdentifier::PerfEventGroupIdentifier(const GatorCpu & cluster, const std::map<int, int> & cpuToTypeMap)
    : cluster(&cluster), cpuNumberToType(&cpuToTypeMap)
{
}

bool PerfEventGroupIdentifier::operator<(const PerfEventGroupIdentifier & that) const
{
    // sort CPU cluster events first
    if (cluster != nullptr) {
        if (that.cluster == nullptr) {
            return true;
        }
        const auto minThis = *std::min_element(cluster->getCpuIds().begin(), cluster->getCpuIds().end());
        const auto minThat = *std::min_element(that.cluster->getCpuIds().begin(), that.cluster->getCpuIds().end());
        if (minThis < minThat) {
            return true;
        }

        if (minThat < minThis) {
            return false;
        }

        if (groupNo < that.groupNo) {
            return true;
        }

        // one without SPE should come before SPE

        if (that.cpuNumberToType == nullptr) {
            return (cpuNumberToType != nullptr);
        }

        if (cpuNumberToType == nullptr) {
            return false;
        }

        if (that.cpuNumberToType->empty()) {
            return !cpuNumberToType->empty();
        }

        return false;
    }
    if (that.cluster != nullptr) {
        return false;
    }

    // sort Uncore PMU events second
    if (pmu != nullptr) {
        return (that.pmu != nullptr ? (strcmp(pmu->getId(), that.pmu->getId()) < 0) : true);
    }
    if (that.pmu != nullptr) {
        return false;
    }

    // sort per-cpu global events next
    if (cpuNumber >= 0) {
        return (that.cpuNumber >= 0 ? (cpuNumber < that.cpuNumber) : true);
    }
    if (that.cpuNumber >= 0) {
        return false;
    }

    // both equal
    return false;
}

PerfEventGroupIdentifier::operator std::string() const
{
    if (cluster != nullptr) {
        auto result = std::string(cluster->getId());
        if (groupNo > 0) {
            result += lib::printf_str_t<32>("##%u", groupNo);
        }
        return result;
    }
    if (pmu != nullptr) {
        return pmu->getId();
    }
    if (cpuNumberToType != nullptr) {
        return "SPE";
    }
    if (cpuNumber >= 0) {
        return lib::Format() << "Software Events on CPU #" << cpuNumber;
    }
    return "Global Software Events";
}
