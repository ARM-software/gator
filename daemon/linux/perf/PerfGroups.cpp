/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "linux/perf/PerfGroups.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <cinttypes>
#include <cstring>
#include <fstream>

#include "Logging.h"
#include "lib/Time.h"
#include "SessionData.h"

PerfGroups::PerfGroups(PerfBuffer * pb, const PerfConfig & config)
        : sharedConfig(config),
          perfEventGroupMap(),
          mPb(pb)
{
}

PerfEventGroup& PerfGroups::addGroupLeader(const uint64_t timestamp, Buffer * const buffer, const PerfEventGroupIdentifier & groupIdentifier) {
    // find the actual group object
    std::unique_ptr<PerfEventGroup> & eventGroupPtr = perfEventGroupMap[groupIdentifier];
    if (!eventGroupPtr) {
        eventGroupPtr.reset(new PerfEventGroup(groupIdentifier, sharedConfig));
    }
    PerfEventGroup & eventGroup = *eventGroupPtr;
    // Does a group exist for this already?
    if (eventGroup.requiresLeader() && !eventGroup.hasLeader()) {
        logg.logMessage("    Adding group leader");
        if (!eventGroup.createGroupLeader(timestamp, buffer)) {
            logg.logMessage("    Group leader not created");
        }
    }
    return eventGroup;
}

bool PerfGroups::add(const uint64_t timestamp, Buffer * const buffer, const PerfEventGroupIdentifier & groupIdentifier, const int key,
                     const uint32_t type, const uint64_t config, uint64_t periodOrFreq, uint64_t sampleType, int flags)
{

    PerfEventGroup & eventGroup = addGroupLeader(timestamp, buffer, groupIdentifier);
    logg.logMessage("Adding event: timestamp=%" PRIu64 ", group='%s', key=%i, type=%" PRIu32 ", config=%" PRIu64 ", period=%" PRIu64 ", sampleType=0x%" PRIx64 ", flags=0x%x",
            timestamp, std::string(groupIdentifier).c_str(), key, type, config, periodOrFreq, sampleType, flags);

    // If we are not system wide the group leader can't read counters for us
    // so we need to add sample them individually periodically
    if (((!sharedConfig.perfConfig.is_system_wide) || (!eventGroup.requiresLeader())) && (periodOrFreq == 0)) {
        logg.logMessage("    Forcing as freq counter");
        periodOrFreq = gSessionData.mSampleRate > 0 && !gSessionData.mIsEBS ? gSessionData.mSampleRate : 10UL;
        sampleType |= PERF_SAMPLE_PERIOD;
        flags |= PERF_GROUP_FREQ;
    }
    logg.logMessage("    Adding event");

    return eventGroup.addEvent(false, timestamp, buffer, key, type, config, periodOrFreq, sampleType, flags);
}

/**
 * Inherently racey function to collect child tids because threads can be created and destroyed while this is running
 *
 * @param tids set of tids to addd this tid and it's children to
 * @param tid
 */
static void addTidsRecursively(std::set<int> & tids, int tid)
{
    auto result = tids.insert(tid);

    if (!result.second)
        return; // we've already added this and its children

    char filename[50];

    // try to get all children (since Linux 3.5)
    snprintf(filename, sizeof(filename), "/proc/%d/task/%d/children", tid, tid);
    std::ifstream children { filename, std::ios_base::in };
    if (children) {
        int child;
        while (children >> child)
            addTidsRecursively(tids, child);
        return;
    }

    // Fall back to just getting threads when children isn't available.
    // New processes won't be counted on onlined cpu.
    // We could read /proc/[pid]/stat for every process and create a map in reverse
    // but that would likely be time consuming
    snprintf(filename, sizeof(filename), "/proc/%d/task", tid);
    DIR * const taskDir = opendir(filename);
    if (taskDir != nullptr) {
        const dirent * taskEntry;
        while ((taskEntry = readdir(taskDir)) != nullptr) {
            // no point recursing if we're relying on the fall back
            if (std::strcmp(taskEntry->d_name, ".") != 0 && std::strcmp(taskEntry->d_name, "..") != 0)
                tids.insert(atoi(taskEntry->d_name));
        }

        return;
    }

    logg.logMessage("Could not read %s", filename);
}

OnlineResult PerfGroups::onlineCPU(const uint64_t timestamp, const int cpu, const std::set<int> & appPids, bool enableNow, Buffer * const buffer,
                                   Monitor * const monitor)
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
            addTidsRecursively(tids, appPid);
        }
    }

    for (auto & pair : perfEventGroupMap) {
        const auto result = pair.second->onlineCPU(timestamp, cpu, tids, enableNow, buffer, monitor, mPb);
        if (result != OnlineResult::SUCCESS) {
            return result;
        }
    }

    return OnlineResult::SUCCESS;
}

bool PerfGroups::offlineCPU(const int cpu)
{
    logg.logMessage("Offlining cpu %i", cpu);

    for (auto & pair : perfEventGroupMap) {
        if (!pair.second->offlineCPU(cpu, mPb)) {
            return false;
        }
    }

    return true;
}

void PerfGroups::start()
{
    if (sharedConfig.perfConfig.is_system_wide) // Non-system-wide is enabled on exec
    {
        for (auto & pair : perfEventGroupMap) {
            pair.second->start();
        }
    }
}

void PerfGroups::stop()
{
    for (auto & pair : perfEventGroupMap) {
        pair.second->stop();
    }
}
