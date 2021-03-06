/* Copyright (C) 2018-2021 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfEventGroup.h"

#include "DynBuf.h"
#include "Logging.h"
#include "SessionData.h"
#include "lib/Format.h"
#include "lib/Optional.h"
#include "lib/Syscall.h"
#include "linux/perf/IPerfAttrsConsumer.h"
#include "linux/perf/PerfUtils.h"
#include "xml/PmuXML.h"

#include <cassert>
#include <cinttypes>
#include <climits>
#include <fcntl.h>
#include <iostream>
#include <set>
#include <sys/ioctl.h>

namespace {
    constexpr unsigned long NANO_SECONDS_IN_ONE_SECOND = 1000000000UL;
    constexpr unsigned long NANO_SECONDS_IN_100_MS = 100000000UL;

    int sys_perf_event_open(struct perf_event_attr * const attr,
                            const pid_t pid,
                            const int cpu,
                            const int group_fd,
                            const unsigned long flags)
    {
        int fd = lib::perf_event_open(attr, pid, cpu, group_fd, flags);
        if (fd < 0) {
            return -1;
        }
        int fdf = lib::fcntl(fd, F_GETFD);
        if ((fdf == -1) || (lib::fcntl(fd, F_SETFD, fdf | FD_CLOEXEC) != 0)) {
            lib::close(fd);
            return -1;
        }
        return fd;
    }

    bool readAndSend(IPerfAttrsConsumer & attrsConsumer,
                     const struct perf_event_attr & attr,
                     const int fd,
                     const int keyCount,
                     const int * const keys)
    {
        for (int retry = 0; retry < 10; ++retry) {
            char buf[1024];
            ssize_t bytes = lib::read(fd, buf, sizeof(buf));
            if (bytes < 0) {
                logg.logMessage("read failed");
                return false;
            }

            if (bytes == 0) {
                /* pinning failed, retry */
                usleep(1);
                continue;
            }

            attrsConsumer.marshalKeysOld(keyCount, keys, bytes, buf);
            return true;
        }

        /* not able to pin even, log and return true, data is skipped */
        logg.logError("Could not pin event %u:0x%llx, skipping", attr.type, attr.config);
        return true;
    }

    std::string perfAttrToString(const perf_event_attr & attr,
                                 const char * typeLabel,
                                 const char * indentation,
                                 const char * separator)
    {
        return (lib::Format() << indentation << "type: " << attr.type << " ("
                              << (typeLabel != nullptr ? typeLabel : "<unk>") << ")" << separator << indentation
                              << "config: " << attr.config << separator << indentation << "config1: " << attr.config1
                              << separator << indentation << "config2: " << attr.config2 << separator << indentation
                              << "sample: " << attr.sample_period << separator << std::hex << indentation
                              << "sample_type: 0x" << attr.sample_type << separator << indentation << "read_format: 0x"
                              << attr.read_format << separator << std::dec << indentation
                              << "pinned: " << (attr.pinned ? "true" : "false") << separator << indentation
                              << "mmap: " << (attr.mmap ? "true" : "false") << separator << indentation
                              << "comm: " << (attr.comm ? "true" : "false") << separator << indentation
                              << "freq: " << (attr.freq ? "true" : "false") << separator << indentation
                              << "task: " << (attr.task ? "true" : "false") << separator << indentation
                              << "exclude_kernel: " << (attr.exclude_kernel ? "true" : "false") << separator
                              << indentation << "enable_on_exec: " << (attr.enable_on_exec ? "true" : "false")
                              << separator << indentation << "inherit: " << (attr.inherit ? "true" : "false")
                              << separator << indentation
                              << "sample_id_all: " << (attr.sample_id_all ? "true" : "false") << separator
                              << indentation << "aux_watermark: " << attr.aux_watermark << separator);
    }
}

PerfEventGroup::PerfEventGroup(const PerfEventGroupIdentifier & groupIdentifier,
                               PerfEventGroupSharedConfig & sharedConfig)
    : groupIdentifier(groupIdentifier), sharedConfig(sharedConfig), events(), cpuToEventIndexToTidToFdMap()
{
}

bool PerfEventGroup::requiresLeader() const
{
    switch (groupIdentifier.getType()) {
        case PerfEventGroupIdentifier::Type::GLOBAL:
        case PerfEventGroupIdentifier::Type::SPECIFIC_CPU:
        case PerfEventGroupIdentifier::Type::SPE:
            return false;
        case PerfEventGroupIdentifier::Type::PER_CLUSTER_CPU:
        case PerfEventGroupIdentifier::Type::UNCORE_PMU:
            return true;
        default:
            assert(false && "Unexpected group type");
            return false;
    }
}

bool PerfEventGroup::hasLeader() const
{
    return requiresLeader() && (!events.empty());
}

bool PerfEventGroup::addEvent(const bool leader,
                              IPerfAttrsConsumer & attrsConsumer,
                              const int key,
                              const IPerfGroups::Attr & attr,
                              bool hasAuxData)
{
    if (leader && !events.empty()) {
        assert(false && "Cannot set leader for non-empty group");
        return false;
    }
    if (events.size() >= INT_MAX) {
        return false;
    }

    events.emplace_back();
    PerfEvent & event = events.back();

    event.attr.size = sizeof(event.attr);
    /* Emit time, read_format below, group leader id, and raw tracepoint info */
    const uint64_t sampleReadMask =
        (sharedConfig.perfConfig.is_system_wide
             ? 0
             : PERF_SAMPLE_READ); // Unfortunately PERF_SAMPLE_READ is not allowed with inherit
    event.attr.sample_type =
        PERF_SAMPLE_TIME |
        (attr.sampleType & ~sampleReadMask)
        // required fields for reading 'id'
        | (sharedConfig.perfConfig.has_sample_identifier ? PERF_SAMPLE_IDENTIFIER
                                                         : PERF_SAMPLE_TID | PERF_SAMPLE_IP | PERF_SAMPLE_ID)
        // see https://lkml.org/lkml/2012/7/18/355
        | (attr.type == PERF_TYPE_TRACEPOINT ? PERF_SAMPLE_PERIOD : 0)
        // always sample TID for application mode; we use it to attribute counter values to their processes
        | (sharedConfig.perfConfig.is_system_wide && !attr.context_switch ? 0 : PERF_SAMPLE_TID)
        // must sample PERIOD is used if 'freq' to read the actual period value
        | (attr.freq ? PERF_SAMPLE_PERIOD : 0);

    // when running in application mode, inherit must always be set, in system wide mode, inherit must always be clear
    event.attr.inherit = (sharedConfig.perfConfig.is_system_wide ? 0 : 1); // make sure all new children are counted too
    event.attr.inherit_stat = event.attr.inherit;
    /* Emit emit value in group format */
    // Unfortunately PERF_FORMAT_GROUP is not allowed with inherit
    event.attr.read_format = PERF_FORMAT_ID | (event.attr.inherit ? 0 : PERF_FORMAT_GROUP);
    // Always be on the CPU but only a perf_event_open group leader can be pinned
    // We can only use perf_event_open groups if PERF_FORMAT_GROUP is used to sample group members
    // If the group has no leader, then all members are in separate perf_event_open groups (and hence their own leader)
    const bool isNotInAReadFormatGroup = ((event.attr.read_format & PERF_FORMAT_GROUP) == 0);
    const bool everyAttributeInGroupIsPinned = (!requiresLeader());
    event.attr.pinned = ((leader || isNotInAReadFormatGroup || everyAttributeInGroupIsPinned) ? 1 : 0);
    // group leader must start disabled, all others enabled
    event.attr.disabled = event.attr.pinned;
    /* have a sampling interrupt happen when we cross the wakeup_watermark boundary */
    event.attr.watermark = 1;
    /* Be conservative in flush size as only one buffer set is monitored */
    event.attr.wakeup_watermark = sharedConfig.dataBufferLength / 2;
    /* Use the monotonic raw clock if possible */
    event.attr.use_clockid = sharedConfig.perfConfig.has_attr_clockid_support ? 1 : 0;
    event.attr.clockid = sharedConfig.perfConfig.has_attr_clockid_support ? CLOCK_MONOTONIC_RAW : 0;
    event.attr.type = attr.type;
    event.attr.config = attr.config;
    event.attr.config1 = attr.config1;
    event.attr.config2 = attr.config2;
    event.attr.sample_period = attr.periodOrFreq;
    event.attr.mmap = attr.mmap;
    event.attr.comm = attr.comm;
    event.attr.freq = attr.freq;
    event.attr.task = attr.task;
    /* sample_id_all should always be set (or should always match pinned); it is required for any non-grouped event, for grouped events it is ignored for anything but the leader */
    event.attr.sample_id_all = 1;
    event.attr.context_switch = attr.context_switch;
    event.attr.exclude_kernel = (sharedConfig.perfConfig.exclude_kernel ? 1 : 0);
    event.attr.exclude_hv = (sharedConfig.perfConfig.exclude_kernel ? 1 : 0);
    event.attr.exclude_idle = (sharedConfig.perfConfig.exclude_kernel ? 1 : 0);
    event.attr.aux_watermark = hasAuxData ? sharedConfig.auxBufferLength / 2 : 0;
    event.key = key;

    // [SDDAP-10625] - trace context switch information for SPE attributes.
    // it is required (particularly in system-wide mode) to be able to see
    // the boundarys of SPE data, as it is not guaranteed to get PERF_RECORD_ITRACE_START
    // between two processes if they are sampled by the same SPE attribute.
    if (groupIdentifier.getType() == PerfEventGroupIdentifier::Type::SPE) {
        if (!sharedConfig.perfConfig.has_attr_context_switch) {
            assert(false && "SPE requires context switch information");
            return false;
        }
        event.attr.context_switch = true;
    }

    attrsConsumer.marshalPea(&event.attr, key);

    return true;
}

bool PerfEventGroup::createGroupLeader(IPerfAttrsConsumer & attrsConsumer)
{
    switch (groupIdentifier.getType()) {
        case PerfEventGroupIdentifier::Type::PER_CLUSTER_CPU:
            return createCpuGroupLeader(attrsConsumer);

        case PerfEventGroupIdentifier::Type::UNCORE_PMU:
            return createUncoreGroupLeader(attrsConsumer);

        case PerfEventGroupIdentifier::Type::SPECIFIC_CPU:
        case PerfEventGroupIdentifier::Type::GLOBAL:
        case PerfEventGroupIdentifier::Type::SPE:
        default:
            assert(false && "Should not be called");
            return false;
    }
}

bool PerfEventGroup::createCpuGroupLeader(IPerfAttrsConsumer & attrsConsumer)
{
    const bool enableCallChain = (sharedConfig.backtraceDepth > 0);

    IPerfGroups::Attr attr {};
    attr.sampleType = PERF_SAMPLE_TID | PERF_SAMPLE_READ;
    attr.mmap = true;
    attr.comm = true;
    attr.task = true;
    bool enableTaskClock = false;

    // [SDDAP-10028] Do not use sched_switch in app tracing mode as it only triggers on switch-out (even when tracing as root)
    if (sharedConfig.perfConfig.can_access_tracepoints && sharedConfig.perfConfig.is_system_wide) {
        // Use sched switch to drive the sampling so that event counts are
        // exactly attributed to each thread in system-wide mode
        if (sharedConfig.schedSwitchId == UNKNOWN_TRACEPOINT_ID) {
            logg.logMessage("Unable to read sched_switch id");
            return false;
        }
        attr.type = PERF_TYPE_TRACEPOINT;
        attr.config = sharedConfig.schedSwitchId;
        attr.periodOrFreq = 1;
        // collect sched switch info from the tracepoint
        attr.sampleType |= PERF_SAMPLE_RAW;
    }
    else {
        attr.type = PERF_TYPE_SOFTWARE;
        if (sharedConfig.perfConfig.has_attr_context_switch) {
            // collect sched switch info directly from perf
            attr.context_switch = true;

            // use dummy as leader if possible
            if (sharedConfig.perfConfig.has_count_sw_dummy) {
                attr.config = PERF_COUNT_SW_DUMMY;
                attr.periodOrFreq = 0;
            }
            // otherwise use sampling as leader
            else {
                attr.config = PERF_COUNT_SW_CPU_CLOCK;
                attr.periodOrFreq = (sharedConfig.sampleRate > 0 && sharedConfig.enablePeriodicSampling
                                         ? NANO_SECONDS_IN_ONE_SECOND / sharedConfig.sampleRate
                                         : 0);
                attr.sampleType |=
                    PERF_SAMPLE_TID | PERF_SAMPLE_IP | PERF_SAMPLE_READ | (enableCallChain ? PERF_SAMPLE_CALLCHAIN : 0);
            }
        }
        else if (!sharedConfig.perfConfig.exclude_kernel) {
            // use context switches as leader. this should give us 'switch-out' events
            attr.config = PERF_COUNT_SW_CONTEXT_SWITCHES;
            attr.periodOrFreq = 1;
            attr.sampleType |= PERF_SAMPLE_TID;
            enableTaskClock = true;
        }
        else {
            // no context switches at all :-(
            attr.config = PERF_COUNT_SW_CPU_CLOCK;
            attr.periodOrFreq = (sharedConfig.sampleRate > 0 && sharedConfig.enablePeriodicSampling
                                     ? NANO_SECONDS_IN_ONE_SECOND / sharedConfig.sampleRate
                                     : 0);
            attr.sampleType |=
                PERF_SAMPLE_TID | PERF_SAMPLE_IP | PERF_SAMPLE_READ | (enableCallChain ? PERF_SAMPLE_CALLCHAIN : 0);
        }
    }

    // Group leader
    if (!addEvent(true, attrsConsumer, sharedConfig.schedSwitchKey, attr, false)) {
        return false;
    }

    // Periodic PC sampling
    if ((attr.config != PERF_COUNT_SW_CPU_CLOCK) && sharedConfig.sampleRate > 0 &&
        sharedConfig.enablePeriodicSampling) {
        IPerfGroups::Attr pcAttr {};
        pcAttr.type = PERF_TYPE_SOFTWARE;
        pcAttr.config = PERF_COUNT_SW_CPU_CLOCK;
        pcAttr.sampleType =
            PERF_SAMPLE_TID | PERF_SAMPLE_IP | PERF_SAMPLE_READ | (enableCallChain ? PERF_SAMPLE_CALLCHAIN : 0);
        pcAttr.periodOrFreq = NANO_SECONDS_IN_ONE_SECOND / sharedConfig.sampleRate;
        if (!addEvent(false, attrsConsumer, nextDummyKey(), pcAttr, false)) {
            return false;
        }
    }

    // use high frequency task clock to attempt to catch the first switch back to a process after a switch out
    // this should give us approximate 'switch-in' events
    if (enableTaskClock) {
        IPerfGroups::Attr taskClockAttr {};
        taskClockAttr.type = PERF_TYPE_SOFTWARE;
        taskClockAttr.config = PERF_COUNT_SW_TASK_CLOCK;
        taskClockAttr.periodOrFreq = 100000UL; // equivalent to 100us
        taskClockAttr.sampleType = PERF_SAMPLE_TID;
        if (!addEvent(false, attrsConsumer, nextDummyKey(), taskClockAttr, false)) {
            return false;
        }
    }

    return true;
}

bool PerfEventGroup::createUncoreGroupLeader(IPerfAttrsConsumer & attrsConsumer)
{
    IPerfGroups::Attr attr {};
    attr.type = PERF_TYPE_SOFTWARE;
    attr.config = PERF_COUNT_SW_CPU_CLOCK;
    attr.sampleType = PERF_SAMPLE_READ;
    // Non-CPU PMUs are sampled every 100ms for Sample Rate: None otherwise they would never be sampled
    attr.periodOrFreq =
        (sharedConfig.sampleRate > 0 ? NANO_SECONDS_IN_ONE_SECOND / sharedConfig.sampleRate : NANO_SECONDS_IN_100_MS);

    return addEvent(true, attrsConsumer, nextDummyKey(), attr, false);
}

static const char * selectTypeLabel(const char * groupLabel, std::uint32_t type)
{
    switch (type) {
        case PERF_TYPE_HARDWARE:
            return "cpu";
        case PERF_TYPE_BREAKPOINT:
            return "breakpoint";
        case PERF_TYPE_HW_CACHE:
            return "hw-cache";
        case PERF_TYPE_RAW:
            return groupLabel;
        case PERF_TYPE_SOFTWARE:
            return "software";
        case PERF_TYPE_TRACEPOINT:
            return "tracepoint";
        default:
            return (type < PERF_TYPE_MAX ? "?" : groupLabel);
    }
}

std::pair<OnlineResult, std::string> PerfEventGroup::onlineCPU(int cpu,
                                                               std::set<int> & tids,
                                                               OnlineEnabledState enabledState,
                                                               IPerfAttrsConsumer & attrsConsumer,
                                                               const std::function<bool(int)> & addToMonitor,
                                                               const std::function<bool(int, int, bool)> & addToBuffer)
{
    if (events.empty()) {
        return std::make_pair(OnlineResult::SUCCESS, "");
    }

    const GatorCpu & cpuCluster = sharedConfig.clusters[sharedConfig.clusterIds[cpu]];
    const GatorCpu * cluster = groupIdentifier.getCluster();
    const UncorePmu * uncorePmu = groupIdentifier.getUncorePmu();
    const std::map<int, int> * cpuNumberToType = groupIdentifier.getSpeTypeMap();

    const char * groupLabel = "?";
    const char * deviceInstance = nullptr;
    bool perCpu = false;

    // validate cpu
    uint32_t replaceType = 0;
    switch (groupIdentifier.getType()) {
        case PerfEventGroupIdentifier::Type::PER_CLUSTER_CPU: {
            groupLabel = cluster->getCoreName();
            perCpu = true;
            if (!(*cluster == cpuCluster)) {
                return std::make_pair(OnlineResult::SUCCESS, "");
            }
            break;
        }

        case PerfEventGroupIdentifier::Type::UNCORE_PMU: {
            groupLabel = uncorePmu->getCoreName();
            deviceInstance = uncorePmu->getDeviceInstance();
            const std::set<int> cpuMask = perf_utils::readCpuMask(uncorePmu->getId());
            const bool currentCpuNotInMask = ((!cpuMask.empty()) && (cpuMask.count(cpu) == 0));
            const bool maskIsEmptyAndCpuNotDefault = (cpuMask.empty() && (cpu != 0));
            if (currentCpuNotInMask || maskIsEmptyAndCpuNotDefault) {
                // SKIP this core without marking an error
                return std::make_pair(OnlineResult::SUCCESS, "");
            }
            break;
        }

        case PerfEventGroupIdentifier::Type::SPE: {
            groupLabel = "SPE";
            perCpu = true;
            const auto & type = cpuNumberToType->find(cpu);
            if (type == cpuNumberToType->end()) {
                return std::make_pair(OnlineResult::SUCCESS, "");
            }
            replaceType = type->second;
            break;
        }

        case PerfEventGroupIdentifier::Type::SPECIFIC_CPU: {
            groupLabel = cpuCluster.getCoreName();
            perCpu = true;
            if (cpu != groupIdentifier.getCpuNumber()) {
                return std::make_pair(OnlineResult::SUCCESS, "");
                ;
            }
            break;
        }

        case PerfEventGroupIdentifier::Type::GLOBAL: {
            groupLabel = "Global";
            break;
        }

        default: {
            assert(false && "Unexpected group type");
            return std::make_pair(OnlineResult::OTHER_FAILURE, "Unexpected group type");
        }
    }

    const bool enableNow = (enabledState == OnlineEnabledState::ENABLE_NOW);
    const bool enableOnExec = (enabledState == OnlineEnabledState::ENABLE_ON_EXEC);

    std::map<int, std::map<int, lib::AutoClosingFd>> eventIndexToTidToFdMap;

    const std::size_t numberOfEvents = events.size();
    for (std::size_t eventIndex = 0; eventIndex < numberOfEvents; ++eventIndex) {
        PerfEvent & event = events[eventIndex];

        if (cpuToEventIndexToTidToFdMap[cpu].count(eventIndex) != 0) {
            std::string message("CPU already online or not correctly cleaned up");
            return std::make_pair(OnlineResult::FAILURE, message);
        }

        const char * typeLabel = selectTypeLabel(groupLabel, event.attr.type);

        // Note we are modifying the attr after we have marshalled it
        // but we are assuming enable_on_exec will be ignored by Streamline
        event.attr.enable_on_exec = (event.attr.pinned && enableOnExec) ? 1 : 0;
        if (replaceType > 0) {
            event.attr.type = replaceType;
        }

        logg.logMessage(
            "Opening attribute:\n"
            "    cpu: %i\n"
            "    key: %i\n"
            "    cluster: %s\n"
            "    index: %" PRIuPTR "\n"
            "    -------------\n"
            "%s",
            cpu,
            event.key,
            (cluster != nullptr ? cluster->getId() : (uncorePmu != nullptr ? uncorePmu->getId() : "<nullptr>")),
            eventIndex,
            perfAttrToString(event.attr, typeLabel, "    ", "\n").c_str());

        for (auto tidsIterator = tids.begin(); tidsIterator != tids.end();) {
            const int tid = *tidsIterator;

            // This assumes that group leader is added first
            const int groupLeaderFd = event.attr.pinned ? -1 : *(eventIndexToTidToFdMap.at(0).at(tid));

            lib::AutoClosingFd fd;

            // try with exclude_kernel clear
            {
                // clear
                event.attr.exclude_kernel = 0;
                event.attr.exclude_hv = 0;
                event.attr.exclude_idle = 0;

                // open event
                fd = sys_perf_event_open(&event.attr,
                                         tid,
                                         cpu,
                                         groupLeaderFd,
                                         // This is "(broken since Linux 2.6.35)" so can possibly be removed
                                         // we use PERF_EVENT_IOC_SET_OUTPUT anyway
                                         PERF_FLAG_FD_OUTPUT);
            }

            // take a copy of errno so that logging calls etc don't overwrite it
            auto peo_errno = errno;

            // retry with just exclude_kernel set
            if ((!fd) && (peo_errno == EACCES)) {
                logg.logMessage("Failed when exclude_kernel == 0, retrying with exclude_kernel = 1");

                // set
                event.attr.exclude_kernel = 1;
                event.attr.exclude_hv = 0;
                event.attr.exclude_idle = 0;

                // open event
                fd = sys_perf_event_open(&event.attr,
                                         tid,
                                         cpu,
                                         groupLeaderFd,
                                         // This is "(broken since Linux 2.6.35)" so can possibly be removed
                                         // we use PERF_EVENT_IOC_SET_OUTPUT anyway
                                         PERF_FLAG_FD_OUTPUT);

                // retry with exclude_kernel and all set
                if ((!fd) && (peo_errno == EACCES)) {
                    logg.logMessage("Failed when exclude_kernel == 1, exclude_hv == 0, exclude_idle == 0, retrying "
                                    "with all exclusions enabled");

                    // set
                    event.attr.exclude_kernel = 1;
                    event.attr.exclude_hv = 1;
                    event.attr.exclude_idle = 1;

                    // open event
                    fd = sys_perf_event_open(&event.attr,
                                             tid,
                                             cpu,
                                             groupLeaderFd,
                                             // This is "(broken since Linux 2.6.35)" so can possibly be removed
                                             // we use PERF_EVENT_IOC_SET_OUTPUT anyway
                                             PERF_FLAG_FD_OUTPUT);

                    // take a new copy of the errno if it failed
                    peo_errno = errno;
                }
            }

            logg.logMessage("perf_event_open: tid: %i, leader = %i -> fd = %i", tid, groupLeaderFd, *fd);

            if (!fd) {
                logg.logMessage("failed (%d) %s", peo_errno, strerror(peo_errno));

                if (peo_errno == ENODEV) {
                    // The core is offline
                    return std::make_pair(OnlineResult::CPU_OFFLINE,
                                          "The event involves a feature not supported by the current CPU.");
                }
                else if (peo_errno == ESRCH) {
                    // thread exited before we had chance to open event
                    tidsIterator = tids.erase(tidsIterator);
                    continue;
                }
                else if ((peo_errno == ENOENT) && (!event.attr.pinned)) {
                    // This event doesn't apply to this CPU but should apply to a different one, e.g. bigLittle
                    goto skipOtherTids;
                }
                std::ostringstream stringStream;

                stringStream << "perf_event_open failed to online counter for " << typeLabel;
                if (deviceInstance != nullptr) {
                    stringStream << " (" << deviceInstance << ")";
                }
                stringStream << " with config=0x" << std::hex << event.attr.config << std::dec;
                if (perCpu) {
                    stringStream << " on CPU " << cpu;
                }
                stringStream << ". Failure given was errno=" << peo_errno << " (" << strerror(peo_errno) << ").";

                if (sharedConfig.perfConfig.is_system_wide) {
                    if (peo_errno == EINVAL) {
                        switch (event.attr.type) {
                            case PERF_TYPE_BREAKPOINT:
                            case PERF_TYPE_SOFTWARE:
                            case PERF_TYPE_TRACEPOINT:
                                break;
                            case PERF_TYPE_HARDWARE:
                            case PERF_TYPE_HW_CACHE:
                            case PERF_TYPE_RAW:
                            default:
                                stringStream
                                    << "\n\nAnother process may be using the PMU counter, or the combination requested "
                                       "may not be supported by the hardware. Try removing some events.";
                                break;
                        }
                    }
                    return std::make_pair(OnlineResult::FAILURE, stringStream.str());
                }
                else {
                    logg.logWarning("%s", stringStream.str().c_str());
                }
            }
            else if (!addToBuffer(*fd, cpu, event.attr.aux_watermark != 0)) {
                std::string message("PerfBuffer::useFd failed");
                if (sharedConfig.perfConfig.is_system_wide) {
                    return std::make_pair(OnlineResult::FAILURE, message.c_str());
                }
                else {
                    logg.logMessage("PerfBuffer::useFd failed");
                }
            }
            else if (!addToMonitor(*fd)) {
                std::string message("Monitor::add failed");
                return std::make_pair(OnlineResult::FAILURE, message.c_str());
            }
            else {
                eventIndexToTidToFdMap[eventIndex][tid] = std::move(fd);
            }

            ++tidsIterator;
        }
    skipOtherTids:;
    }

    if (sharedConfig.perfConfig.has_ioctl_read_id) {
        bool addedEvents = false;
        std::vector<int> coreKeys;
        std::vector<uint64_t> ids;

        for (const auto & eventIndexToTidToFdPair : eventIndexToTidToFdMap) {
            const int eventIndex = eventIndexToTidToFdPair.first;
            const PerfEvent & event = events.at(eventIndex);
            const int key = event.key;

            for (const auto & tidToFdPair : eventIndexToTidToFdPair.second) {
                const auto & fd = tidToFdPair.second;

                // get the id
                uint64_t id = 0;
                if (lib::ioctl(*fd, PERF_EVENT_IOC_ID, reinterpret_cast<unsigned long>(&id)) != 0 &&
                    // Workaround for running 32-bit gatord on 64-bit systems, kernel patch in the works
                    lib::ioctl(*fd,
                               (PERF_EVENT_IOC_ID & ~IOCSIZE_MASK) | (8 << _IOC_SIZESHIFT),
                               reinterpret_cast<unsigned long>(&id)) != 0) {
                    std::string message("ioctl failed");
                    logg.logMessage("%s", message.c_str());
                    return std::make_pair(OnlineResult::OTHER_FAILURE, message.c_str());
                }

                // store it
                coreKeys.push_back(key);
                ids.emplace_back(id);

                // log it
                logg.logMessage("Perf id for key : %i, fd : %i  -->  %" PRIu64, key, *fd, id);

                addedEvents = true;
            }
        }

        if (!addedEvents) {
            logg.logMessage("no events came online");
        }

        attrsConsumer.marshalKeys(ids.size(), ids.data(), coreKeys.data());
    }
    else {
        std::vector<int> keysInGroup;

        // send the ungrouped attributes, collect keys for grouped attributes
        for (const auto & eventIndexToTidToFdPair : eventIndexToTidToFdMap) {
            const int eventIndex = eventIndexToTidToFdPair.first;
            const PerfEvent & event = events.at(eventIndex);
            const bool isLeader = requiresLeader() && (eventIndex == 0);

            if (event.attr.pinned && !isLeader) {
                for (const auto & tidToFdPair : eventIndexToTidToFdPair.second) {
                    const auto & fd = tidToFdPair.second;
                    if (!readAndSend(attrsConsumer, event.attr, *fd, 1, &event.key)) {
                        return std::make_pair(OnlineResult::OTHER_FAILURE, "read failed");
                    }
                }
            }
            else {
                keysInGroup.push_back(event.key);
            }
        }

        assert((requiresLeader() || keysInGroup.empty()) && "Cannot read group items without leader");

        // send the grouped attributes and their keys
        if (!keysInGroup.empty()) {
            const auto & event = events.at(0);
            const auto & tidToFdMap = eventIndexToTidToFdMap.at(0);
            for (const auto & tidToFdPair : tidToFdMap) {
                const auto & fd = tidToFdPair.second;
                if (!readAndSend(attrsConsumer, event.attr, *fd, keysInGroup.size(), keysInGroup.data())) {
                    return std::make_pair(OnlineResult::OTHER_FAILURE, "read failed");
                }
            }
        }
    }

    if (enableNow) {
        if (!enable(eventIndexToTidToFdMap) || !checkEnabled(eventIndexToTidToFdMap)) {
            return std::make_pair(OnlineResult::OTHER_FAILURE, "Unable to enable a perf event");
        }
    }

    // everything enabled successfully, move into map
    cpuToEventIndexToTidToFdMap[cpu] = std::move(eventIndexToTidToFdMap);

    return std::make_pair(OnlineResult::SUCCESS, "");
}

bool PerfEventGroup::offlineCPU(int cpu)
{
    auto & eventIndexToTidToFdMap = cpuToEventIndexToTidToFdMap[cpu];

    // we disable in the opposite order that we enabled for some reason
    const auto eventIndexToTidToFdRend = eventIndexToTidToFdMap.rend();
    for (auto eventIndexToTidToFdIt = eventIndexToTidToFdMap.rbegin(); eventIndexToTidToFdIt != eventIndexToTidToFdRend;
         ++eventIndexToTidToFdIt) {
        const auto & tidToFdMap = eventIndexToTidToFdIt->second;
        const auto tidToFdRend = tidToFdMap.rend();
        for (auto tidToFdIt = tidToFdMap.rbegin(); tidToFdIt != tidToFdRend; ++tidToFdIt) {
            const auto & fd = tidToFdIt->second;
            if (lib::ioctl(*fd, PERF_EVENT_IOC_DISABLE, 0) != 0) {
                logg.logMessage("ioctl failed");
                return false;
            }
        }
    }

    // close all the fds
    eventIndexToTidToFdMap.clear();

    return true;
}

bool PerfEventGroup::enable(const std::map<int, std::map<int, lib::AutoClosingFd>> & eventIndexToTidToFdMap)
{
    // Enable group leaders, others should be enabled by default
    for (const auto & eventIndexToTidToFdPair : eventIndexToTidToFdMap) {
        const int eventIndex = eventIndexToTidToFdPair.first;
        const PerfEvent & event = events.at(eventIndex);

        for (const auto & tidToFdPair : eventIndexToTidToFdPair.second) {
            const auto & fd = tidToFdPair.second;

            if (event.attr.pinned && (lib::ioctl(*fd, PERF_EVENT_IOC_ENABLE, 0) != 0)) {
                logg.logError("Unable to enable a perf event");
                return false;
            }
        }
    }
    return true;
}

bool PerfEventGroup::checkEnabled(const std::map<int, std::map<int, lib::AutoClosingFd>> & eventIndexToTidToFdMap)
{
    // Try reading from all the group leaders to ensure that the event isn't disabled
    char buf[1 << 10];
    int readResultCount = 0;

    // Enable group leaders, others should be enabled by default
    for (const auto & eventIndexToTidToFdPair : eventIndexToTidToFdMap) {
        const int eventIndex = eventIndexToTidToFdPair.first;
        const PerfEvent & event = events.at(eventIndex);

        for (const auto & tidToFdPair : eventIndexToTidToFdPair.second) {
            const auto tid = tidToFdPair.first;
            const auto & fd = tidToFdPair.second;

            if (event.attr.pinned) {
                const auto readResult = lib::read(*fd, buf, sizeof(buf));
                if (readResult < 0) {
                    logg.logError("Unable to read all perf groups, perhaps too many events were enabled (%d, %s)",
                                  errno,
                                  strerror(errno));
                    return false;
                }
                else if (readResult == 0) {
                    ++readResultCount;

                    logg.logWarning("Unable to enable a perf group, pinned group marked as in disabled due to conflict "
                                    "or insufficient resources. (%d: tid = %d, fd = %d, attr = \n%s)",
                                    eventIndex,
                                    tid,
                                    *fd,
                                    perfAttrToString(event.attr, nullptr, "    ", "\n").c_str());
                }
            }
        }
    }

    // log an error message on the console to the user telling them that some items were disabled.
    if (readResultCount > 0) {
        logg.logError(
            "Unable to enable %d perf groups due to them being reported as being disabled due to conflict or "
            "insufficient resources.\n"
            "Another process may be using one or more perf counters.\n"
            "Use `lsof|grep perf_event` (if available) to find other processes that may be using perf counters.\n"
            "Not all event data may be available in the capture.\n"
            "See debug log for more information.",
            readResultCount);
    }

    return true;
}

void PerfEventGroup::start()
{
    // Enable everything before checking to avoid losing data
    for (const auto & cpuToEventIndexToTidToFdPair : cpuToEventIndexToTidToFdMap) {
        if (!enable(cpuToEventIndexToTidToFdPair.second)) {
            handleException();
        }
    }
    for (const auto & cpuToEventIndexToTidToFdPair : cpuToEventIndexToTidToFdMap) {
        if (!checkEnabled(cpuToEventIndexToTidToFdPair.second)) {
            handleException();
        }
    }
}

void PerfEventGroup::stop()
{
    for (const auto & cpuToEventIndexToTidToFdPair : cpuToEventIndexToTidToFdMap) {
        const auto & eventIndexToTidToFdMap = cpuToEventIndexToTidToFdPair.second;
        const auto eventIndexToTidToFdRend = eventIndexToTidToFdMap.rend();
        for (auto eventIndexToTidToFdIt = eventIndexToTidToFdMap.rbegin();
             eventIndexToTidToFdIt != eventIndexToTidToFdRend;
             ++eventIndexToTidToFdIt) {
            const auto & tidToFdMap = eventIndexToTidToFdIt->second;
            const auto tidToFdRend = tidToFdMap.rend();
            for (auto tidToFdIt = tidToFdMap.rbegin(); tidToFdIt != tidToFdRend; ++tidToFdIt) {
                const auto & fd = tidToFdIt->second;
                lib::ioctl(*fd, PERF_EVENT_IOC_DISABLE, 0);
            }
        }
    }
}

int PerfEventGroup::nextDummyKey()
{
    return sharedConfig.dummyKeyCounter--;
}
