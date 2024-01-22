/* Copyright (C) 2013-2023 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfGroups.h"

#include "Logging.h"
#include "k/perf_event.h"
#include "lib/Assert.h"
#include "linux/perf/IPerfGroups.h"
#include "linux/perf/PerfEventGroup.h"
#include "linux/perf/PerfEventGroupIdentifier.h"
#include "linux/perf/attr_to_key_mapping_tracker.h"

#include <cinttypes>
#include <string>

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
            state.numberOfEventsAdded += it->second.events.size();
        }
    }

    return eventGroup;
}

void perf_groups_configurer_t::initHeader(attr_to_key_mapping_tracker_t & mapping_tracker)
{
    IPerfGroups::Attr attr {};
    attr.type = PERF_TYPE_SOFTWARE;
    attr.config = (configuration.perfConfig.has_count_sw_dummy ? PERF_COUNT_SW_DUMMY : PERF_COUNT_SW_CPU_CLOCK);
    attr.periodOrFreq = 0;
    attr.sampleType = 0;
    attr.comm = true;
    attr.task = true;
    attr.mmap = true;

    auto result = perf_event_group_configurer_t::initEvent(configuration,
                                                           state.header,
                                                           true,
                                                           false,
                                                           PerfEventGroupIdentifier::Type::GLOBAL,
                                                           true,
                                                           mapping_tracker,
                                                           perf_event_group_configurer_t::nextDummyKey(configuration),
                                                           attr,
                                                           false);

    runtime_assert(result, "Failed to init header event");
}

bool perf_groups_configurer_t::add(attr_to_key_mapping_tracker_t & mapping_tracker,
                                   const PerfEventGroupIdentifier & groupIdentifier,
                                   const int key,
                                   const IPerfGroups::Attr & attr,
                                   bool hasAuxData)
{

    auto eventGroup = getGroup(mapping_tracker, groupIdentifier);
    LOG_FINE("Adding event: group='%s', key=%i, type=%" PRIu32 ", config=%" PRIu64 ", config1=%" PRIu64
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
