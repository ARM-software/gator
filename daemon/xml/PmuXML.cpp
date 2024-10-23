/* Copyright (C) 2010-2024 by Arm Limited. All rights reserved. */

#include "xml/PmuXML.h"

#include "SessionData.h"
#include "lib/Assert.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

GatorCpu::GatorCpu(std::string coreName,
                   std::string id,
                   std::string counterSet,
                   const char * dtName,
                   const char * speName,
                   const char * speVersion,
                   const std::set<int> & cpuIds,
                   int pmncCounters,
                   bool isV8)
    : mCoreName(std::move(coreName)),
      mId(std::move(id)),
      mCounterSet(std::move(counterSet)),
      mDtName(dtName != nullptr ? dtName : ""),
      mSpeName(speName != nullptr ? speName : ""),
      mSpeVersion(speVersion != nullptr ? speVersion : ""),
      mCpuIds(cpuIds.begin(), cpuIds.end()),
      mPmncCounters(gSessionData.mOverrideNoPmuSlots > 0 ? gSessionData.mOverrideNoPmuSlots : pmncCounters),
      mIsV8(isV8)
{
    runtime_assert(!mCpuIds.empty(), "got pmu without cpuids");
    std::sort(mCpuIds.begin(), mCpuIds.end());
}

GatorCpu::GatorCpu(std::string coreName,
                   std::string id,
                   std::string counterSet,
                   std::string dtName,
                   std::string speName,
                   std::string speVersion,
                   std::vector<int> cpuIds,
                   int pmncCounters,
                   bool isV8)
    : mCoreName(std::move(coreName)),
      mId(std::move(id)),
      mCounterSet(std::move(counterSet)),
      mDtName(std::move(dtName)),
      mSpeName(std::move(speName)),
      mSpeVersion(std::move(speVersion)),
      mCpuIds(std::move(cpuIds)),
      mPmncCounters(gSessionData.mOverrideNoPmuSlots > 0 ? gSessionData.mOverrideNoPmuSlots : pmncCounters),
      mIsV8(isV8)
{
}

GatorCpu::GatorCpu(const GatorCpu & that, const char * speName, const char * speVersion)
    : mCoreName(that.mCoreName),
      mId(that.mId),
      mCounterSet(that.mCounterSet),
      mDtName(that.mDtName),
      mSpeName(speName),
      mSpeVersion(speVersion),
      mCpuIds(that.mCpuIds),
      mPmncCounters(that.mPmncCounters),
      mIsV8(that.mIsV8)
{
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
