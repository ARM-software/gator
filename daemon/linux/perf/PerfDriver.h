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
#include <functional>
#include <stdint.h>

#include "ClassBoilerPlate.h"
#include "SimpleDriver.h"
#include "linux/perf/PerfConfig.h"
#include "linux/perf/PerfDriverConfiguration.h"

static constexpr const char * SCHED_SWITCH = "sched/sched_switch";
static constexpr const char * CPU_IDLE = "power/cpu_idle";
static constexpr const char * CPU_FREQUENCY = "power/cpu_frequency";

class ISummaryConsumer;
class GatorCpu;
class IPerfGroups;
class IPerfAttrsConsumer;
class PerfTracepoint;
class UncorePmu;
class ICpuInfo;

class PerfDriver : public SimpleDriver
{
public:
    PerfDriver(PerfDriverConfiguration && configuration, PmuXML && pmuXml, const char * maliFamilyName, const ICpuInfo & cpuInfo);
    ~PerfDriver();

    const PerfConfig & getConfig() const
    {
        return mConfig.config;
    }


    void readEvents(mxml_node_t * const xml) override;
    int writeCounters(mxml_node_t *root) const override;
    bool summary(ISummaryConsumer & consumer, std::function<uint64_t()> getAndSetMonotonicStarted);
    void coreName(const uint64_t currTime, ISummaryConsumer & consumer, const int cpu);
    void setupCounter(Counter &counter) override;
    lib::Optional<CapturedSpe> setupSpe(const SpeConfiguration & spe) override;
    bool enable(const uint64_t currTime, IPerfGroups & group, IPerfAttrsConsumer & attrsConsumer) const;
    void read(IPerfAttrsConsumer & attrsConsumer, const int cpu);
    bool sendTracepointFormats(const uint64_t currTime, IPerfAttrsConsumer & attrsConsumer);

private:
    void addCpuCounters(const PerfCpu & cpu);
    void addUncoreCounters(const PerfUncore & uncore);
    PerfTracepoint *mTracepoints;
    bool mIsSetup;
    PerfDriverConfiguration mConfig;
    PmuXML mPmuXml;
    const ICpuInfo & mCpuInfo;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(PerfDriver);

    void addMidgardHwTracepoints(const char * const maliFamilyName);
};

#endif // PERFDRIVER_H
