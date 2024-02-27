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

PerfEventGroupIdentifier::PerfEventGroupIdentifier(const std::map<int, int> & cpuToTypeMap)
    : cpuNumberToType(&cpuToTypeMap)
{
}

bool PerfEventGroupIdentifier::operator<(const PerfEventGroupIdentifier & that) const
{
    // sort CPU cluster events first
    if (cluster != nullptr) {
        if (that.cluster == nullptr) {
            return true;
        }
        const int minThis = *std::min_element(cluster->getCpuIds().begin(), cluster->getCpuIds().end());
        const int minThat = *std::min_element(that.cluster->getCpuIds().begin(), that.cluster->getCpuIds().end());
        if (minThis < minThat) {
            return true;
        }

        if (minThis > minThat) {
            return false;
        }

        return groupNo < that.groupNo;
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

    if (cpuNumberToType != nullptr) {
        return (that.cpuNumberToType != nullptr ? (cpuNumberToType < that.cpuNumberToType) : true);
    }
    if (that.cpuNumberToType != nullptr) {
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
