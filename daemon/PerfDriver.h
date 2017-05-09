/**
 * Copyright (C) ARM Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PERFDRIVER_H
#define PERFDRIVER_H

#include <list>
#include <memory>
#include <stdint.h>

#include "ClassBoilerPlate.h"
#include "SimpleDriver.h"

#define SCHED_SWITCH "sched/sched_switch"
#define CPU_IDLE "power/cpu_idle"
#define CPU_FREQUENCY "power/cpu_frequency"

class Buffer;
class DynBuf;
class GatorCpu;
class PerfGroup;
class PerfTracepoint;
class UncorePmu;

class PerfDriver : public SimpleDriver
{
public:

    /**
     * Contains the detected parameters of perf
     */
    class PerfDriverConfiguration
    {
    private:

        // opaque to everyone else
        friend class PerfDriver;

        std::list<GatorCpu *> cpuPmus;
        std::list<UncorePmu *> uncorePmus;
        bool foundCpu;
        bool legacySupport;
        bool clockidSupport;

        PerfDriverConfiguration();
    };

    static std::unique_ptr<PerfDriverConfiguration> detect();

    PerfDriver(const PerfDriverConfiguration & configuration);
    ~PerfDriver();

    bool getLegacySupport() const
    {
        return mLegacySupport;
    }
    bool getClockidSupport() const
    {
        return mClockidSupport;
    }

    void readEvents(mxml_node_t * const xml);
    bool summary(Buffer * const buffer);
    void coreName(const uint64_t currTime, Buffer * const buffer, const int cpu);
    void setupCounter(Counter &counter);
    bool enable(const uint64_t currTime, PerfGroup * const group, Buffer * const buffer) const;
    void read(Buffer * const buffer, const int cpu);
    bool sendTracepointFormats(const uint64_t currTime, Buffer * const buffer, DynBuf * const printb, DynBuf * const b);

    static long long getTracepointId(const char * const name, DynBuf * const printb);
    static long long getTracepointId(const char * const counter, const char * const name, DynBuf * const printb);

private:
    void addCpuCounters(const GatorCpu * const cpu);
    void addUncoreCounters(const char * const counterName, const int type, const int numCounters,
                           const bool hasCyclesCounter);
    PerfTracepoint *mTracepoints;
    bool mIsSetup, mLegacySupport, mClockidSupport;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(PerfDriver);

    void addMidgardHwTracepoints(const char * const maliFamilyName);
};

#endif // PERFDRIVER_H
