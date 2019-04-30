/**
 * Copyright (C) Arm Limited 2010-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "PmuXML.h"

#include <cstring>


GatorCpu::GatorCpu(const char * const coreName, const char * const pmncName, const char * const dtName,
                   const char * speName, const int cpuid, const int pmncCounters, const bool isV8)
        : mCoreName(coreName),
          mPmncName(pmncName),
          mDtName(dtName),
          mSpeName(speName),
          mCpuid(cpuid),
          mPmncCounters(pmncCounters),
          mIsV8(isV8)
{
}

static const char OLD_PMU_PREFIX[] = "ARMv7 Cortex-";
static const char NEW_PMU_PREFIX[] = "ARMv7_Cortex_";

const GatorCpu * PmuXML::findCpuByName(const char * const name) const
{
    for (const GatorCpu & gatorCpu : cpus) {
        if (strcasecmp(gatorCpu.getPmncName(), name) == 0 ||
        // Do these names match but have the old vs new prefix?
                ((strncasecmp(name, OLD_PMU_PREFIX, sizeof(OLD_PMU_PREFIX) - 1) == 0
                        && strncasecmp(gatorCpu.getPmncName(), NEW_PMU_PREFIX, sizeof(NEW_PMU_PREFIX) - 1) == 0
                        && strcasecmp(name + sizeof(OLD_PMU_PREFIX) - 1,
                                      gatorCpu.getPmncName() + sizeof(NEW_PMU_PREFIX) - 1) == 0))) {
            return &gatorCpu;
        }
    }

    return nullptr;
}

const GatorCpu * PmuXML::findCpuById(const int cpuid) const
{
    for (const GatorCpu & gatorCpu : cpus) {
        if (gatorCpu.getCpuid() == cpuid) {
            return &gatorCpu;
        }
    }

    return nullptr;
}

UncorePmu::UncorePmu(const char * const coreName, const char * const pmncName, const int pmncCounters,
                     const bool hasCyclesCounter)
        : mCoreName(coreName),
          mPmncName(pmncName),
          mPmncCounters(pmncCounters),
          mHasCyclesCounter(hasCyclesCounter)
{
}

const UncorePmu *PmuXML::findUncoreByName(const char * const name) const
{
    for (const UncorePmu & uncore : uncores) {
        if (strcasecmp(name, uncore.getPmncName()) == 0) {
            return &uncore;
        }
    }
    return nullptr;
}
