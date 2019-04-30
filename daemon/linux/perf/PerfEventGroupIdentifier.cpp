/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfEventGroupIdentifier.h"
#include "lib/Format.h"
#include "PmuXML.h"

#include <cassert>

PerfEventGroupIdentifier::PerfEventGroupIdentifier()
    : cluster(nullptr),
      pmu(nullptr),
      cpuNumber(-1),
      cpuNumberToType(nullptr)
{
}

PerfEventGroupIdentifier::PerfEventGroupIdentifier(const GatorCpu & cluster)
    : cluster(&cluster),
      pmu(nullptr),
      cpuNumber(-1),
      cpuNumberToType(nullptr)
{
}

PerfEventGroupIdentifier::PerfEventGroupIdentifier(const UncorePmu & pmu)
    : cluster(nullptr),
      pmu(&pmu),
      cpuNumber(-1),
      cpuNumberToType(nullptr)
{
}

PerfEventGroupIdentifier::PerfEventGroupIdentifier(int cpuNumber)
    : cluster(nullptr),
      pmu(nullptr),
      cpuNumber(cpuNumber),
      cpuNumberToType(nullptr)
{
    assert (cpuNumber >= 0);
}

PerfEventGroupIdentifier::PerfEventGroupIdentifier(const std::map<int, int> & cpuToTypeMap)
    : cluster(nullptr),
      pmu(nullptr),
      cpuNumber(-1),
      cpuNumberToType(&cpuToTypeMap)
{
}

bool PerfEventGroupIdentifier::operator < (const PerfEventGroupIdentifier & that) const
{
    // sort CPU cluster events first
    if (cluster != nullptr) {
        return (that.cluster != nullptr ? (cluster->getCpuid() < that.cluster->getCpuid())
                                        : true);
    }
    else if (that.cluster != nullptr) {
        return false;
    }

    // sort Uncore PMU events second
    if (pmu != nullptr) {
        return (that.pmu != nullptr ? (strcmp(pmu->getPmncName(), that.pmu->getPmncName()) < 0)
                                    : true);
    }
    else if (that.pmu != nullptr) {
        return false;
    }

    // sort per-cpu global events next
    if (cpuNumber >= 0) {
        return (that.cpuNumber >= 0 ? (cpuNumber < that.cpuNumber)
                                    : true);
    }
    else if (that.cpuNumber >= 0) {
        return false;
    }

    if (cpuNumberToType != nullptr) {
        return (that.cpuNumberToType != nullptr ? (cpuNumberToType < that.cpuNumberToType)
                                    : true);
    }
    else if (that.cpuNumberToType != nullptr) {
        return false;
    }

    // both equal
    return false;
}

PerfEventGroupIdentifier::operator std::string () const
{
    if (cluster != nullptr) {
        return cluster->getPmncName();
    }
    else if (pmu != nullptr) {
        return pmu->getPmncName();
    }
    else if (cpuNumberToType != nullptr) {
        return "SPE";
    }
    else if (cpuNumber >= 0) {
        return lib::Format() << "Software Events on CPU #" << cpuNumber;
    }
    else {
        return "Global Software Events";
    }
}
