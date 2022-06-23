/* Copyright (C) 2013-2022 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfGroups.h"

#include "Logging.h"
#include "linux/perf/PerfEventGroup.h"

#include <cassert>
#include <cerrno>
#include <cinttypes>
#include <climits>
#include <cstring>
#include <iterator>
#include <numeric>

#include <sys/resource.h>
#include <unistd.h>

perf_event_group_configurer_t perf_groups_configurer_t::getGroup(attr_to_key_mapping_tracker_t & mapping_tracker,
                                                                 const PerfEventGroupIdentifier & groupIdentifier)
{
    auto it_inserted = state.perfEventGroupMap.try_emplace(groupIdentifier);
    auto & it = it_inserted.first;

    perf_event_group_configurer_t eventGroup {configuration, it->first, it->second};

    // Does a group exist for this already?
    if (eventGroup.requiresLeader() && !eventGroup.hasLeader()) {
        LOG_DEBUG("    Adding group leader");
        if (!eventGroup.createGroupLeader(mapping_tracker)) {
            LOG_DEBUG("    Group leader not created");
        }
        else {
            state.numberOfEventsAdded += it->second.common.events.size();
        }
    }

    return eventGroup;
}

bool perf_groups_configurer_t::add(attr_to_key_mapping_tracker_t & mapping_tracker,
                                   const PerfEventGroupIdentifier & groupIdentifier,
                                   const int key,
                                   const IPerfGroups::Attr & attr,
                                   bool hasAuxData)
{

    auto eventGroup = getGroup(mapping_tracker, groupIdentifier);
    LOG_DEBUG("Adding event: group='%s', key=%i, type=%" PRIu32 ", config=%" PRIu64 ", config1=%" PRIu64
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
            PERF_SAMPLE_TID | PERF_SAMPLE_IP | (configuration.backtraceDepth > 0 ? PERF_SAMPLE_CALLCHAIN : 0);
    }

    // If we are not system wide the group leader can't read counters for us
    // so we need to add sample them individually periodically
    if (((!configuration.perfConfig.is_system_wide) || (!eventGroup.requiresLeader())) && (attr.periodOrFreq == 0)) {
        LOG_DEBUG("    Forcing as freq counter");
        newAttr.periodOrFreq =
            configuration.sampleRate > 0 && configuration.enablePeriodicSampling ? configuration.sampleRate : 10UL;
        newAttr.sampleType |= PERF_SAMPLE_PERIOD;
        newAttr.freq = true;
    }

    state.numberOfEventsAdded++;

    LOG_DEBUG("    Adding event");

    return eventGroup.addEvent(false, mapping_tracker, key, newAttr, hasAuxData);
}

bool perf_groups_activator_t::hasSPE() const
{
    for (const auto & pair : state.perfEventGroupMap) {
        if (pair.first.getType() == PerfEventGroupIdentifier::Type::SPE) {
            return true;
        }
    }

    return false;
}

std::size_t perf_groups_activator_t::getMaxFileDescriptors()
{
    // Get the maximum amount of file descriptors that can be opened.
    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
        LOG_ERROR("getrlimit failed: %s", strerror(errno));
        handleException();
    }

    const rlim_t numberOfFdsReservedForGatord = 150;
    // rlim_cur should've been set to rlim_max in main.cpp
    if (rlim.rlim_cur < numberOfFdsReservedForGatord) {
        LOG_ERROR("Not enough file descriptors to run gatord. Must have a minimum of %" PRIuMAX
                  " (currently the limit is %" PRIuMAX ").",
                  static_cast<uintmax_t>(numberOfFdsReservedForGatord),
                  static_cast<uintmax_t>(rlim.rlim_cur));
        handleException();
    }

    return std::size_t(rlim.rlim_cur - numberOfFdsReservedForGatord);
}

std::pair<OnlineResult, std::string> perf_groups_activator_t::onlineCPU(
    int cpu,
    const std::set<int> & appPids,
    OnlineEnabledState enabledState,
    id_to_key_mapping_tracker_t & mapping_tracker,
    const std::function<bool(int)> & addToMonitor,
    const std::function<bool(int, int, bool)> & addToBuffer,
    const std::function<std::set<int>(int)> & childTids)
{
    LOG_DEBUG("Onlining cpu %i", cpu);
    if (!configuration.perfConfig.is_system_wide && appPids.empty()) {
        std::string message("No task given for non-system-wide");
        return std::make_pair(OnlineResult::FAILURE, message.c_str());
    }

    std::set<int> tids {};
    if (configuration.perfConfig.is_system_wide) {
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
    const unsigned int amountOfEventsAboutToOpen = tids.size() * state.numberOfEventsAdded;
    eventsOpenedPerCpu[cpu] = amountOfEventsAboutToOpen;
    const unsigned int currentAmountOfEvents = std::accumulate(
        std::begin(eventsOpenedPerCpu),
        std::end(eventsOpenedPerCpu),
        0,
        [](unsigned int total, const std::map<int, unsigned int>::value_type & p) { return total + p.second; });

    if (maxFiles < currentAmountOfEvents) {
        LOG_ERROR("Not enough file descriptors for the amount of events requested.");
        handleException();
    }

    for (auto & pair : state.perfEventGroupMap) {
        perf_event_group_activator_t activator {configuration, pair.first, pair.second};
        const auto result = activator.onlineCPU(cpu, tids, enabledState, mapping_tracker, addToMonitor, addToBuffer);
        if (result.first != OnlineResult::SUCCESS) {
            return result;
        }
    }
    return std::make_pair(OnlineResult::SUCCESS, "");
}

bool perf_groups_activator_t::offlineCPU(int cpu, const std::function<void(int)> & removeFromBuffer)
{
    LOG_DEBUG("Offlining cpu %i", cpu);

    for (auto & pair : state.perfEventGroupMap) {
        perf_event_group_activator_t activator {configuration, pair.first, pair.second};
        if (!activator.offlineCPU(cpu)) {
            return false;
        }
    }

    // Mark the buffer so that it will be released next time it's read
    removeFromBuffer(cpu);

    eventsOpenedPerCpu.erase(cpu);

    return true;
}

void perf_groups_activator_t::start()
{
    for (auto & pair : state.perfEventGroupMap) {
        perf_event_group_activator_t activator {configuration, pair.first, pair.second};
        activator.start();
    }
}

void perf_groups_activator_t::stop()
{
    for (auto & pair : state.perfEventGroupMap) {
        perf_event_group_activator_t activator {configuration, pair.first, pair.second};
        activator.stop();
    }
}
