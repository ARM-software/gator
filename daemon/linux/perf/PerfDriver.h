/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PERFDRIVER_H
#define PERFDRIVER_H

#include <list>
#include <memory>
#include <set>
#include <stdint.h>

#include "ClassBoilerPlate.h"
#include "SimpleDriver.h"
#include "linux/perf/PerfConfig.h"

#define SCHED_SWITCH "sched/sched_switch"
#define CPU_IDLE "power/cpu_idle"
#define CPU_FREQUENCY "power/cpu_frequency"

class Buffer;
class DynBuf;
class GatorCpu;
class PerfGroups;
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
        PerfConfig config;

        PerfDriverConfiguration();
    };

    static std::unique_ptr<PerfDriverConfiguration> detect(bool systemWide);

    PerfDriver(const PerfDriverConfiguration & configuration);
    ~PerfDriver();

    const PerfConfig & getConfig() const
    {
        return mConfig;
    }


    void readEvents(mxml_node_t * const xml);
    bool summary(Buffer * const buffer);
    void coreName(const uint64_t currTime, Buffer * const buffer, const int cpu);
    void setupCounter(Counter &counter);
    bool enable(const uint64_t currTime, PerfGroups * const group, Buffer * const buffer) const;
    void read(Buffer * const buffer, const int cpu);
    bool sendTracepointFormats(const uint64_t currTime, Buffer * const buffer, DynBuf * const printb, DynBuf * const b);

    static long long getTracepointId(const char * const name, DynBuf * const printb);
    static long long getTracepointId(const char * const counter, const char * const name, DynBuf * const printb);

private:
    void addCpuCounters(const GatorCpu * cpu);
    void addUncoreCounters(const UncorePmu * pmu);
    PerfTracepoint *mTracepoints;
    bool mIsSetup;
    PerfConfig mConfig;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(PerfDriver);

    void addMidgardHwTracepoints(const char * const maliFamilyName);
};

#endif // PERFDRIVER_H
