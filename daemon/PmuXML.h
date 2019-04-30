/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PMUXML_H
#define PMUXML_H

#include <vector>
#include <set>

#include "ClassBoilerPlate.h"

class GatorCpu
{
public:
    GatorCpu(const char * const coreName, const char * const pmncName, const char * const dtName, const char * speName,
             const int cpuid, const int pmncCounters, const bool isV8);

    CLASS_DEFAULT_COPY_MOVE(GatorCpu);

    const char *getCoreName() const
    {
        return mCoreName;
    }

    const char *getPmncName() const
    {
        return mPmncName;
    }

    const char *getDtName() const
    {
        return mDtName;
    }

    const char *getSpeName() const
    {
        return mSpeName;
    }

    bool getIsV8() const
    {
        return mIsV8;
    }

    int getCpuid() const
    {
        return mCpuid;
    }

    int getPmncCounters() const
    {
        return mPmncCounters;
    }

    bool operator==(const GatorCpu & other) const
    {
        return mCpuid == other.mCpuid;
    }

    bool operator<(const GatorCpu & other) const
    {
        return mCpuid < other.mCpuid;
    }

private:
    const char * mCoreName;
    const char * mPmncName;
    const char * mDtName;
    const char * mSpeName;
    int mCpuid;
    int mPmncCounters;
    bool mIsV8;
};

class UncorePmu
{
public:
    UncorePmu(const char * const coreName, const char * const pmncName, const int pmncCounters,
              const bool hasCyclesCounter);

    CLASS_DEFAULT_COPY_MOVE(UncorePmu);

    const char *getCoreName() const
    {
        return mCoreName;
    }

    const char *getPmncName() const
    {
        return mPmncName;
    }

    int getPmncCounters() const
    {
        return mPmncCounters;
    }

    bool getHasCyclesCounter() const
    {
        return mHasCyclesCounter;
    }

private:
    const char * mCoreName;
    const char * mPmncName;
    int mPmncCounters;
    bool mHasCyclesCounter;
};

struct PmuXML
{
    const GatorCpu *findCpuByName(const char * const name) const;
    const GatorCpu *findCpuById(const int cpuid) const;
    const UncorePmu *findUncoreByName(const char * const name) const;

    std::vector<GatorCpu> cpus;
    std::vector<UncorePmu> uncores;
};

#endif // PMUXML_H
