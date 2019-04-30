/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "linux/perf/PerfGroups.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <cinttypes>

#include "Logging.h"

PerfGroups::PerfGroups(const PerfConfig & perfConfig, size_t bufferLength, int backtraceDepth, int sampleRate,
                       bool isEbs, lib::Span<const GatorCpu> clusters, lib::Span<const int> clusterIds, int64_t schedSwitchId)
        : sharedConfig(perfConfig, bufferLength, backtraceDepth, sampleRate, isEbs, clusters, clusterIds, schedSwitchId),
          perfEventGroupMap()
{
}

PerfEventGroup& PerfGroups::getGroup(const uint64_t timestamp, IPerfAttrsConsumer & attrsConsumer, const PerfEventGroupIdentifier & groupIdentifier) {
    // find the actual group object
    std::unique_ptr<PerfEventGroup> & eventGroupPtr = perfEventGroupMap[groupIdentifier];
    if (!eventGroupPtr) {
        eventGroupPtr.reset(new PerfEventGroup(groupIdentifier, sharedConfig));
    }
    PerfEventGroup & eventGroup = *eventGroupPtr;
    // Does a group exist for this already?
    if (eventGroup.requiresLeader() && !eventGroup.hasLeader()) {
        logg.logMessage("    Adding group leader");
        if (!eventGroup.createGroupLeader(timestamp, attrsConsumer)) {
            logg.logMessage("    Group leader not created");
        }
    }
    return eventGroup;
}

bool PerfGroups::add(const uint64_t timestamp, IPerfAttrsConsumer & attrsConsumer, const PerfEventGroupIdentifier & groupIdentifier, const int key,
                     const IPerfGroups::Attr & attr, bool hasAuxData)
{

    PerfEventGroup & eventGroup = getGroup(timestamp, attrsConsumer, groupIdentifier);
    logg.logMessage("Adding event: timestamp=%" PRIu64 ", group='%s', key=%i, type=%" PRIu32 ", config=%" PRIu64 ", config1=%" PRIu64 ", config2=%" PRIu64 ", period=%" PRIu64 ", sampleType=0x%" PRIx64
            ", mmap=%d, comm=%d, freq=%d, task=%d, context_switch=%d, hasAuxData=%d",
            timestamp, std::string(groupIdentifier).c_str(), key, attr.type, attr.config, attr.config1, attr.config2, attr.periodOrFreq, attr.sampleType,
            attr.mmap,
            attr.comm,
            attr.freq,
            attr.task,
            attr.context_switch,
            hasAuxData);

    IPerfGroups::Attr newAttr {attr};

    // Collect EBS samples
    if (attr.periodOrFreq != 0) {
        newAttr.sampleType |= PERF_SAMPLE_TID | PERF_SAMPLE_IP | (sharedConfig.backtraceDepth > 0 ? PERF_SAMPLE_CALLCHAIN : 0);
    }

    // If we are not system wide the group leader can't read counters for us
    // so we need to add sample them individually periodically
    if (((!sharedConfig.perfConfig.is_system_wide) || (!eventGroup.requiresLeader())) && (attr.periodOrFreq == 0)) {
        logg.logMessage("    Forcing as freq counter");
        newAttr.periodOrFreq = sharedConfig.sampleRate > 0 && !sharedConfig.isEbs ? sharedConfig.sampleRate : 10UL;
        newAttr.sampleType |= PERF_SAMPLE_PERIOD;
        newAttr.freq = true;
    }
    logg.logMessage("    Adding event");

    return eventGroup.addEvent(false, timestamp, attrsConsumer, key, newAttr, hasAuxData);
}

OnlineResult PerfGroups::onlineCPU(uint64_t timestamp, int cpu, const std::set<int> & appPids, OnlineEnabledState enabledState,
                                   IPerfAttrsConsumer & attrsConsumer, std::function<bool(int)> addToMonitor,
                                   std::function<bool(int, int, bool)> addToBuffer,
                                   std::function<std::set<int>(int)> childTids)
{
    logg.logMessage("Onlining cpu %i", cpu);

    if (!sharedConfig.perfConfig.is_system_wide && appPids.empty()) {
        logg.logMessage("No task given for non-system-wide");
        return OnlineResult::FAILURE;
    }

    std::set<int> tids { };
    if (sharedConfig.perfConfig.is_system_wide) {
        tids.insert(-1);
    }
    else {
        for (int appPid : appPids) {
            for (int tid : childTids(appPid)) {
                tids.insert(tid);
            }
        }
    }

    for (auto & pair : perfEventGroupMap) {
        const auto result = pair.second->onlineCPU(timestamp, cpu, tids, enabledState, attrsConsumer, addToMonitor, addToBuffer);
        if (result != OnlineResult::SUCCESS) {
            return result;
        }
    }

    return OnlineResult::SUCCESS;
}

bool PerfGroups::offlineCPU(int cpu, std::function<void(int)> removeFromBuffer)
{
    logg.logMessage("Offlining cpu %i", cpu);

    for (auto & pair : perfEventGroupMap) {
        if (!pair.second->offlineCPU(cpu)) {
            return false;
        }
    }

    // Mark the buffer so that it will be released next time it's read
    removeFromBuffer(cpu);

    return true;
}

void PerfGroups::start()
{
    for (auto & pair : perfEventGroupMap) {
        pair.second->start();
    }
}

void PerfGroups::stop()
{
    for (auto & pair : perfEventGroupMap) {
        pair.second->stop();
    }
}

bool PerfGroups::hasSPE() const
{
    for (auto & pair : perfEventGroupMap) {
        if (pair.first.getType() == PerfEventGroupIdentifier::Type::SPE) {
            return true;
        }
    }

    return false;
}
