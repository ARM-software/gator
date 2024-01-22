/* Copyright (C) 2010-2023 by Arm Limited. All rights reserved. */

#include "xml/PmuXML.h"

#include "lib/Assert.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <utility>

GatorCpu::GatorCpu(std::string coreName,
                   std::string id,
                   std::string counterSet,
                   const char * dtName,
                   const char * speName,
                   const std::set<int> & cpuIds,
                   int pmncCounters,
                   bool isV8)
    : mCoreName(std::move(coreName)),
      mId(std::move(id)),
      mCounterSet(std::move(counterSet)),
      mDtName(dtName != nullptr ? dtName : ""),
      mSpeName(speName != nullptr ? speName : ""),
      mCpuIds(cpuIds.begin(), cpuIds.end()),
      mPmncCounters(pmncCounters),
      mIsV8(isV8)
{
    runtime_assert(!mCpuIds.empty(), "got pmu without cpuids");
    std::sort(mCpuIds.begin(), mCpuIds.end());
}

bool GatorCpu::hasCpuId(int cpuId) const
{
    return (std::find(mCpuIds.begin(), mCpuIds.end(), cpuId) != mCpuIds.end());
}

bool operator==(const GatorCpu & a, const GatorCpu & b)
{
    return a.getCpuIds() == b.getCpuIds();
}

bool operator<(const GatorCpu & a, const GatorCpu & b)
{
    return std::lexicographical_compare(a.getCpuIds().begin(),
                                        a.getCpuIds().end(),
                                        b.getCpuIds().begin(),
                                        b.getCpuIds().end());
}

static const char OLD_PMU_PREFIX[] = "ARMv7 Cortex-";
static const char NEW_PMU_PREFIX[] = "ARMv7_Cortex_";

const GatorCpu * PmuXML::findCpuByName(const char * const name) const
{
    for (const GatorCpu & gatorCpu : cpus) {
        if (strcasecmp(gatorCpu.getId(), name) == 0) {
            return &gatorCpu;
        }

        // Do these names match but have the old vs new prefix?
        if (((strncasecmp(name, OLD_PMU_PREFIX, sizeof(OLD_PMU_PREFIX) - 1) == 0)
             && (strncasecmp(gatorCpu.getId(), NEW_PMU_PREFIX, sizeof(NEW_PMU_PREFIX) - 1) == 0)
             && (strcasecmp(name + sizeof(OLD_PMU_PREFIX) - 1, gatorCpu.getId() + sizeof(NEW_PMU_PREFIX) - 1) == 0))) {
            return &gatorCpu;
        }
    }

    return nullptr;
}

const GatorCpu * PmuXML::findCpuById(const int cpuid) const
{
    for (const GatorCpu & gatorCpu : cpus) {
        if (gatorCpu.hasCpuId(cpuid)) {
            return &gatorCpu;
        }
    }

    return nullptr;
}

UncorePmu::UncorePmu(std::string coreName,
                     std::string id,
                     std::string counterSet,
                     std::string deviceInstance,
                     int pmncCounters,
                     bool hasCyclesCounter)
    : mCoreName(std::move(coreName)),
      mId(std::move(id)),
      mCounterSet(std::move(counterSet)),
      mDeviceInstance(std::move(deviceInstance)),
      mPmncCounters(pmncCounters),
      mHasCyclesCounter(hasCyclesCounter)
{
}

const UncorePmu * PmuXML::findUncoreByName(const char * const name) const
{
    for (const UncorePmu & uncore : uncores) {
        if (strcasecmp(name, uncore.getId()) == 0) {
            return &uncore;
        }
    }
    return nullptr;
}
