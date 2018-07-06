/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfEventGroupIdentifier.h"
#include "lib/Format.h"
#include "SessionData.h"

#include <cassert>

PerfEventGroupIdentifier::PerfEventGroupIdentifier()
    : cluster(nullptr),
      pmu(nullptr),
      cpuNumber(-1)
{
}

PerfEventGroupIdentifier::PerfEventGroupIdentifier(const GatorCpu & cluster)
    : cluster(&cluster),
      pmu(nullptr),
      cpuNumber(-1)
{
}

PerfEventGroupIdentifier::PerfEventGroupIdentifier(const UncorePmu & pmu)
    : cluster(nullptr),
      pmu(&pmu),
      cpuNumber(-1)
{
}

PerfEventGroupIdentifier::PerfEventGroupIdentifier(int cpuNumber)
    : cluster(nullptr),
      pmu(nullptr),
      cpuNumber(cpuNumber)
{
    assert (cpuNumber >= 0);
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
    else if (cpuNumber >= 0) {
        return lib::Format() << "Software Events on CPU #" << cpuNumber;
    }
    else {
        return "Global Software Events";
    }
}
