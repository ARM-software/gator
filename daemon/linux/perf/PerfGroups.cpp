/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfGroups.h"

#include "Logging.h"

#include <cassert>
#include <cerrno>
#include <cinttypes>
#include <climits>
#include <cstring>
#include <iterator>
#include <numeric>
#include <sys/resource.h>
#include <unistd.h>

static unsigned int getMaxFileDescriptors()
{
    // Get the maximum amount of file descriptors that can be opened.
    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
        logg.logError("getrlimit failed: %s", strerror(errno));
        handleException();
    }

    const rlim_t numberOfFdsReservedForGatord = 150;
    // rlim_cur should've been set to rlim_max in main.cpp
    if (rlim.rlim_cur < numberOfFdsReservedForGatord) {
        logg.logError("Not enough file descriptors to run gatord. Must have a minimum of %" PRIuMAX
                      " (currently the limit is %" PRIuMAX ").",
                      static_cast<uintmax_t>(numberOfFdsReservedForGatord),
                      static_cast<uintmax_t>(rlim.rlim_cur));
        handleException();
    }
    return static_cast<unsigned int>(rlim.rlim_cur - numberOfFdsReservedForGatord);
}

PerfGroups::PerfGroups(const PerfConfig & perfConfig,
                       size_t dataBufferLength,
                       size_t auxBufferLength,
                       int backtraceDepth,
                       int sampleRate,
                       bool enablePeriodicSampling,
                       lib::Span<const GatorCpu> clusters,
                       lib::Span<const int> clusterIds,
                       int64_t schedSwitchId)
    : PerfGroups(perfConfig,
                 dataBufferLength,
                 auxBufferLength,
                 backtraceDepth,
                 sampleRate,
                 enablePeriodicSampling,
                 clusters,
                 clusterIds,
                 schedSwitchId,
                 getMaxFileDescriptors())
{
}

PerfGroups::PerfGroups(const PerfConfig & perfConfig,
                       size_t dataBufferLength,
                       size_t auxBufferLength,
                       int backtraceDepth,
                       int sampleRate,
                       bool enablePeriodicSampling,
                       lib::Span<const GatorCpu> clusters,
                       lib::Span<const int> clusterIds,
                       int64_t schedSwitchId,
                       unsigned int maxFiles)
    : sharedConfig(perfConfig,
                   dataBufferLength,
                   auxBufferLength,
                   backtraceDepth,
                   sampleRate,
                   enablePeriodicSampling,
                   clusters,
                   clusterIds,
                   schedSwitchId),
      perfEventGroupMap(),
      eventsOpenedPerCpu(),
      maxFiles(maxFiles),
      numberOfEventsAdded(0)
{
}

PerfEventGroup & PerfGroups::getGroup(IPerfAttrsConsumer & attrsConsumer,
                                      const PerfEventGroupIdentifier & groupIdentifier)
{
    // find the actual group object
    std::unique_ptr<PerfEventGroup> & eventGroupPtr = perfEventGroupMap[groupIdentifier];
    if (!eventGroupPtr) {
        eventGroupPtr.reset(new PerfEventGroup(groupIdentifier, sharedConfig));
    }
    PerfEventGroup & eventGroup = *eventGroupPtr;
    // Does a group exist for this already?
    if (eventGroup.requiresLeader() && !eventGroup.hasLeader()) {
        logg.logMessage("    Adding group leader");
        if (!eventGroup.createGroupLeader(attrsConsumer)) {
            logg.logMessage("    Group leader not created");
        }
        else {
            numberOfEventsAdded++;
        }
    }
    return eventGroup;
}

bool PerfGroups::add(IPerfAttrsConsumer & attrsConsumer,
                     const PerfEventGroupIdentifier & groupIdentifier,
                     const int key,
                     const IPerfGroups::Attr & attr,
                     bool hasAuxData)
{

    PerfEventGroup & eventGroup = getGroup(attrsConsumer, groupIdentifier);
    logg.logMessage("Adding event: group='%s', key=%i, type=%" PRIu32 ", config=%" PRIu64 ", config1=%" PRIu64
                    ", config2=%" PRIu64 ", period=%" PRIu64 ", sampleType=0x%" PRIx64
                    ", mmap=%d, comm=%d, freq=%d, task=%d, context_switch=%d, hasAuxData=%d",
                    std::string(groupIdentifier).c_str(),
                    key,
                    attr.type,
                    attr.config,
                    attr.config1,
                    attr.config2,
                    attr.periodOrFreq,
                    attr.sampleType,
                    attr.mmap,
                    attr.comm,
                    attr.freq,
                    attr.task,
                    attr.context_switch,
                    hasAuxData);

    IPerfGroups::Attr newAttr {attr};

    // Collect EBS samples
    if (attr.periodOrFreq != 0) {
        newAttr.sampleType |=
            PERF_SAMPLE_TID | PERF_SAMPLE_IP | (sharedConfig.backtraceDepth > 0 ? PERF_SAMPLE_CALLCHAIN : 0);
    }

    // If we are not system wide the group leader can't read counters for us
    // so we need to add sample them individually periodically
    if (((!sharedConfig.perfConfig.is_system_wide) || (!eventGroup.requiresLeader())) && (attr.periodOrFreq == 0)) {
        logg.logMessage("    Forcing as freq counter");
        newAttr.periodOrFreq =
            sharedConfig.sampleRate > 0 && sharedConfig.enablePeriodicSampling ? sharedConfig.sampleRate : 10UL;
        newAttr.sampleType |= PERF_SAMPLE_PERIOD;
        newAttr.freq = true;
    }

    numberOfEventsAdded++;

    logg.logMessage("    Adding event");

    return eventGroup.addEvent(false, attrsConsumer, key, newAttr, hasAuxData);
}

std::pair<OnlineResult, std::string> PerfGroups::onlineCPU(int cpu,
                                                           const std::set<int> & appPids,
                                                           OnlineEnabledState enabledState,
                                                           IPerfAttrsConsumer & attrsConsumer,
                                                           const std::function<bool(int)> & addToMonitor,
                                                           const std::function<bool(int, int, bool)> & addToBuffer,
                                                           const std::function<std::set<int>(int)> & childTids)
{
    logg.logMessage("Onlining cpu %i", cpu);
    if (!sharedConfig.perfConfig.is_system_wide && appPids.empty()) {
        std::string message("No task given for non-system-wide");
        return std::make_pair(OnlineResult::FAILURE, message.c_str());
    }

    std::set<int> tids {};
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

    // Check to see if there are too many events/ not enough fds
    // This is an over estimation because not every event will be opened.
    const unsigned int amountOfEventsAboutToOpen = tids.size() * numberOfEventsAdded;
    eventsOpenedPerCpu[cpu] = amountOfEventsAboutToOpen;
    const unsigned int currentAmountOfEvents = std::accumulate(
        std::begin(eventsOpenedPerCpu),
        std::end(eventsOpenedPerCpu),
        0,
        [](unsigned int total, const std::map<int, unsigned int>::value_type & p) { return total + p.second; });

    if (maxFiles < currentAmountOfEvents) {
        logg.logError("Not enough file descriptors for the amount of events requested.");
        handleException();
    }

    for (auto & pair : perfEventGroupMap) {
        const auto result = pair.second->onlineCPU(cpu, tids, enabledState, attrsConsumer, addToMonitor, addToBuffer);
        if (result.first != OnlineResult::SUCCESS) {
            return result;
        }
    }
    return std::make_pair(OnlineResult::SUCCESS, "");
}

bool PerfGroups::offlineCPU(int cpu, const std::function<void(int)> & removeFromBuffer)
{
    logg.logMessage("Offlining cpu %i", cpu);

    for (auto & pair : perfEventGroupMap) {
        if (!pair.second->offlineCPU(cpu)) {
            return false;
        }
    }

    // Mark the buffer so that it will be released next time it's read
    removeFromBuffer(cpu);

    eventsOpenedPerCpu.erase(cpu);

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
    for (const auto & pair : perfEventGroupMap) {
        if (pair.first.getType() == PerfEventGroupIdentifier::Type::SPE) {
            return true;
        }
    }

    return false;
}
