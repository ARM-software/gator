/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfEventGroup.h"
#include "linux/perf/PerfBuffer.h"
#include "Buffer.h"
#include "DynBuf.h"
#include "Logging.h"
#include "Monitor.h"
#include "SessionData.h"

#include <cassert>
#include <cinttypes>
#include <climits>
#include <set>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>

namespace
{
    static constexpr unsigned long NANO_SECONDS_IN_ONE_SECOND = 1000000000UL;
    static constexpr unsigned long NANO_SECONDS_IN_100_MS = 100000000UL;

    static const int schedSwitchKey = getEventKey();

    static int sys_perf_event_open(struct perf_event_attr * const attr, const pid_t pid, const int cpu, const int group_fd, const unsigned long flags)
    {
        int fd = syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
        if (fd < 0) {
            return -1;
        }
        int fdf = fcntl(fd, F_GETFD);
        if ((fdf == -1) || (fcntl(fd, F_SETFD, fdf | FD_CLOEXEC) != 0)) {
            close(fd);
            return -1;
        }
        return fd;
    }

    static bool readAndSend(const uint64_t currTime, Buffer * const buffer, const struct perf_event_attr & attr, const int fd, const int keyCount,
                            const int * const keys)
    {
        for (int retry = 0; retry < 10; ++retry) {
            char buf[1024];
            ssize_t bytes = read(fd, buf, sizeof(buf));
            if (bytes < 0) {
                logg.logMessage("read failed");
                return false;
            }

            if (bytes == 0) {
                /* pinning failed, retry */
                usleep(1);
                continue;
            }

            buffer->marshalKeysOld(currTime, keyCount, keys, bytes, buf);
            return true;
        }

        /* not able to pin even, log and return true, data is skipped */
        logg.logError("Could not pin event %u:0x%llx, skipping", attr.type, attr.config);
        return true;
    }
}

PerfEventGroup::PerfEventGroup(const PerfEventGroupIdentifier & groupIdentifier, PerfEventGroupSharedConfig & sharedConfig)
        : groupIdentifier(groupIdentifier),
          sharedConfig(sharedConfig),
          events(),
          cpuToEventIndexToTidToFdMap()
{
}

bool PerfEventGroup::requiresLeader() const
{
    switch (groupIdentifier.getType()) {
        case PerfEventGroupIdentifier::Type::GLOBAL:
        case PerfEventGroupIdentifier::Type::SPECIFIC_CPU:
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

bool PerfEventGroup::addEvent(const bool leader, const uint64_t timestamp, Buffer * const buffer, const int key, const uint32_t type, const uint64_t config,
                              const uint64_t frequencyOrPeriod, const uint64_t sampleType, const int flags)
{
    if (leader && !events.empty()) {
        assert(false && "Cannot set leader for non-empty group");
        return false;
    }
    if (events.size() >= INT_MAX) {
        return false;
    }

    const size_t bufferLength = PerfBuffer::calculateBufferLength();

    events.emplace_back();
    PerfEvent & event = events.back();

    event.attr.size = sizeof(event.attr);
    /* Emit time, read_format below, group leader id, and raw tracepoint info */
    const uint64_t sampleReadMask = (sharedConfig.perfConfig.is_system_wide ? 0 : PERF_SAMPLE_READ); // Unfortunately PERF_SAMPLE_READ is not allowed with inherit
    event.attr.sample_type = PERF_SAMPLE_TIME | (sampleType & ~sampleReadMask)
    // required fields for reading 'id'
            | (sharedConfig.perfConfig.has_sample_identifier ? PERF_SAMPLE_IDENTIFIER : PERF_SAMPLE_TID | PERF_SAMPLE_IP | PERF_SAMPLE_ID)
            // see https://lkml.org/lkml/2012/7/18/355
            | (type == PERF_TYPE_TRACEPOINT ? PERF_SAMPLE_PERIOD : 0)
            // always sample TID if cannot sample context switches
            | (sharedConfig.perfConfig.can_access_tracepoints || sharedConfig.perfConfig.has_attr_context_switch ? 0 : PERF_SAMPLE_TID)
            // must sample PERIOD is used if 'freq' to read the actual period value
            | (flags & PERF_GROUP_FREQ ? PERF_SAMPLE_PERIOD : 0);

    /* Emit emit value in group format */
    event.attr.read_format = PERF_FORMAT_ID | (sharedConfig.perfConfig.is_system_wide ? PERF_FORMAT_GROUP : 0); // Unfortunately PERF_FORMAT_GROUP is not allow with inherit
    /* have a sampling interrupt happen when we cross the wakeup_watermark boundary */
    event.attr.watermark = 1;
    /* Be conservative in flush size as only one buffer set is monitored */
    event.attr.wakeup_watermark = bufferLength / 2;
    /* Use the monotonic raw clock if possible */
    event.attr.use_clockid = sharedConfig.perfConfig.has_attr_clockid_support ? 1 : 0;
    event.attr.clockid = sharedConfig.perfConfig.has_attr_clockid_support ? CLOCK_MONOTONIC_RAW : 0;
    event.attr.type = type;
    event.attr.config = config;
    event.attr.sample_period = frequencyOrPeriod;
    // always be on the CPU but only a group leader can be pinned
    event.attr.pinned = (leader ? 1 : 0);
    // group leader must start disabled, all others enabled
    event.attr.disabled = (leader ? 1 : 0);
    event.attr.mmap = (flags & PERF_GROUP_MMAP ? 1 : 0);
    event.attr.comm = (flags & PERF_GROUP_COMM ? 1 : 0);
    event.attr.freq = (flags & PERF_GROUP_FREQ ? 1 : 0);
    event.attr.task = (flags & PERF_GROUP_TASK ? 1 : 0);
    event.attr.sample_id_all = (flags & PERF_GROUP_SAMPLE_ID_ALL ? 1 : 0);
    event.attr.context_switch = (flags & PERF_GROUP_CONTEXT_SWITCH ? 1 : 0);
    event.attr.exclude_kernel = (sharedConfig.perfConfig.exclude_kernel ? 1 : 0);
    event.attr.exclude_hv = (sharedConfig.perfConfig.exclude_kernel ? 1 : 0);
    event.attr.exclude_idle = (sharedConfig.perfConfig.exclude_kernel ? 1 : 0);
    event.attr.inherit = (sharedConfig.perfConfig.is_system_wide ? 0 : 1); // make sure all new children are counted too
    event.attr.inherit_stat = event.attr.inherit;
    event.flags = flags;
    event.key = key;

    buffer->marshalPea(timestamp, &event.attr, key);

    return true;
}

bool PerfEventGroup::createGroupLeader(uint64_t timestamp, Buffer * buffer)
{
    switch (groupIdentifier.getType()) {
        case PerfEventGroupIdentifier::Type::PER_CLUSTER_CPU:
            return createCpuGroupLeader(timestamp, buffer);

        case PerfEventGroupIdentifier::Type::UNCORE_PMU:
            return createUncoreGroupLeader(timestamp, buffer);

        case PerfEventGroupIdentifier::Type::SPECIFIC_CPU:
        case PerfEventGroupIdentifier::Type::GLOBAL:
        default:
            assert(false && "Should not be called");
            return false;
    }
}

bool PerfEventGroup::createCpuGroupLeader(const uint64_t timestamp, Buffer * const buffer)
{
    const bool enableCallChain = (gSessionData.mBacktraceDepth > 0);

    uint32_t type;
    uint64_t config;
    uint64_t count;
    uint64_t sampleType = PERF_SAMPLE_TID | PERF_SAMPLE_READ;
    bool enableTaskClock = false;
    int flags = PERF_GROUP_MMAP | PERF_GROUP_COMM | PERF_GROUP_TASK | PERF_GROUP_SAMPLE_ID_ALL;

    if (sharedConfig.perfConfig.can_access_tracepoints) {
        // Use sched switch to drive the sampling so that event counts are
        // exactly attributed to each thread in system-wide mode
        if (sharedConfig.schedSwitchId < 0) {
            DynBuf b;
            sharedConfig.schedSwitchId = PerfDriver::getTracepointId(SCHED_SWITCH, &b);
            if (sharedConfig.schedSwitchId < 0) {
                logg.logMessage("Unable to read sched_switch id");
                return false;
            }
        }
        type = PERF_TYPE_TRACEPOINT;
        config = sharedConfig.schedSwitchId;
        count = 1;
        // collect sched switch info from the tracepoint
        sampleType |= PERF_SAMPLE_RAW;
    }
    else {
        type = PERF_TYPE_SOFTWARE;
        if (sharedConfig.perfConfig.has_attr_context_switch) {
            // collect sched switch info directly from perf
            flags |= PERF_GROUP_CONTEXT_SWITCH;

            // use dummy as leader if possible
            if (sharedConfig.perfConfig.has_count_sw_dummy) {
                config = PERF_COUNT_SW_DUMMY;
                count = 0;
            }
            // otherwise use sampling as leader
            else {
                config = PERF_COUNT_SW_CPU_CLOCK;
                count = (gSessionData.mSampleRate > 0 && !gSessionData.mIsEBS ? NANO_SECONDS_IN_ONE_SECOND / gSessionData.mSampleRate : 0);
                sampleType |= PERF_SAMPLE_TID | PERF_SAMPLE_IP | PERF_SAMPLE_READ | (enableCallChain ? PERF_SAMPLE_CALLCHAIN : 0);
            }
        }
        else {
            // use context switches as leader. this should give us 'switch-out' events
            config = PERF_COUNT_SW_CONTEXT_SWITCHES;
            count = 1;
            sampleType |= PERF_SAMPLE_TID;
            enableTaskClock = true;
        }
    }

    // Group leader
    if (!addEvent(true, timestamp, buffer, schedSwitchKey, type, config, count, sampleType, flags)) {
        return false;
    }

    // Periodic PC sampling
    if ((config != PERF_COUNT_SW_CPU_CLOCK) && gSessionData.mSampleRate > 0 && !gSessionData.mIsEBS) {
        if (!addEvent(false, timestamp, buffer, nextDummyKey(), PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK,
                      NANO_SECONDS_IN_ONE_SECOND / gSessionData.mSampleRate,
                      PERF_SAMPLE_TID | PERF_SAMPLE_IP | PERF_SAMPLE_READ | (enableCallChain ? PERF_SAMPLE_CALLCHAIN : 0), 0)) {
            return false;
        }
    }

    // use high frequency task clock to attempt to catch the first switch back to a process after a switch out
    // this should give us approximate 'switch-in' events
    if (enableTaskClock) {
        if (!addEvent(false, timestamp, buffer, nextDummyKey(), PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK, 100000ul, // equivalent to 100us
                      PERF_SAMPLE_TID, 0)) {
            return false;
        }
    }

    return true;
}

bool PerfEventGroup::createUncoreGroupLeader(const uint64_t timestamp, Buffer * const buffer)
{
    // Non-CPU PMUs are sampled every 100ms for Sample Rate: None otherwise they would never be sampled
    const uint64_t timeout = (gSessionData.mSampleRate > 0 ? NANO_SECONDS_IN_ONE_SECOND / gSessionData.mSampleRate : NANO_SECONDS_IN_100_MS);

    return addEvent(true, timestamp, buffer, nextDummyKey(), PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK, timeout, PERF_SAMPLE_READ, 0);
}

bool PerfEventGroup::neverInGroup(int eventIndex) const
{
    return (!sharedConfig.perfConfig.is_system_wide) || (!requiresLeader()) || (eventIndex == 0)
            || ((events.at(eventIndex).attr.read_format & PERF_FORMAT_GROUP) == 0);
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

OnlineResult PerfEventGroup::onlineCPU(const uint64_t timestamp, const int cpu, std::set<int> & tids, const bool enableNow, Buffer * const buffer,
                                       Monitor * const monitor, PerfBuffer * const perfBuffer)
{
    if (events.empty()) {
        return OnlineResult::SUCCESS;
    }

    const GatorCpu * cpuCluster = gSessionData.mSharedData->mClusters[gSessionData.mSharedData->mClusterIds[cpu]];
    const GatorCpu * cluster = groupIdentifier.getCluster();
    const UncorePmu * uncorePmu = groupIdentifier.getUncorePmu();

    const char * groupLabel = "?";

    // validate cpu
    switch (groupIdentifier.getType()) {
        case PerfEventGroupIdentifier::Type::PER_CLUSTER_CPU: {
            groupLabel = cluster->getCoreName();
            if (cluster != cpuCluster) {
                return OnlineResult::SUCCESS;
            }
            break;
        }

        case PerfEventGroupIdentifier::Type::UNCORE_PMU: {
            groupLabel = uncorePmu->getCoreName();
            const std::set<int> cpuMask = uncorePmu->getCpuMask();
            if ((!cpuMask.empty()) && (cpuMask.count(cpu) == 0)) {
                return OnlineResult::SUCCESS;
            }
            else if (cpuMask.empty() && (cpu != 0)) {
                return OnlineResult::SUCCESS;
            }
            break;
        }

        case PerfEventGroupIdentifier::Type::SPECIFIC_CPU: {
            groupLabel = cpuCluster->getCoreName();
            if (cpu != groupIdentifier.getCpuNumber()) {
                return OnlineResult::SUCCESS;
            }
            break;
        }

        case PerfEventGroupIdentifier::Type::GLOBAL: {
            groupLabel = "Global";
            break;
        }

        default: {
            assert(false && "Unexpected group type");
            return OnlineResult::OTHER_FAILURE;
        }
    }

    const bool enableOnExec = !sharedConfig.perfConfig.is_system_wide && !enableNow;

    std::map<int, std::map<int, lib::AutoClosingFd>> eventIndexToTidToFdMap;

    const std::size_t numberOfEvents = events.size();
    for (std::size_t eventIndex = 0; eventIndex < numberOfEvents; ++eventIndex) {
        PerfEvent & event = events[eventIndex];

        if (cpuToEventIndexToTidToFdMap[cpu].count(eventIndex) != 0) {
            logg.logMessage("cpu already online or not correctly cleaned up");
            return OnlineResult::FAILURE;
        }

        const bool pinned = event.attr.pinned;
        const bool notInGroup = neverInGroup(eventIndex);
        const char * typeLabel = selectTypeLabel(groupLabel, event.attr.type);

        // Note we are modifying the attr after we have marshalled it
        // but we are assuming enable_on_exec will be ignored by Streamline
        event.attr.enable_on_exec = (event.attr.pinned && enableOnExec) ? 1 : 0;

        logg.logMessage("Opening attribute:\n"
                "    cpu: %i\n"
                "    flags: 0x%x\n"
                "    key: %i\n"
                "    cluster: %s\n"
                "    index: %" PRIuPTR "\n"
                "    -------------\n"
                "    type: %i (%s)\n"
                "    config: %llu\n"
                "    sample: %llu\n"
                "    sample_type: 0x%llx\n"
                "    read_format: 0x%llx\n"
                "    pinned: %llu\n"
                "    mmap: %llu\n"
                "    comm: %llu\n"
                "    freq: %llu\n"
                "    task: %llu\n"
                "    exclude_kernel: %llu\n"
                "    inherit: %llu\n"
                "    sample_id_all: %llu",
                cpu, event.flags, event.key,
                (cluster != nullptr ? cluster->getPmncName()
                        : (uncorePmu != nullptr ? uncorePmu->getPmncName()
                                : "<nullptr>")),
                eventIndex,
                event.attr.type, typeLabel, event.attr.config, event.attr.sample_period, event.attr.sample_type, event.attr.read_format,
                event.attr.pinned, event.attr.mmap, event.attr.comm, event.attr.freq, event.attr.task, event.attr.exclude_kernel, event.attr.inherit, event.attr.sample_id_all);

        for (auto tidsIterator = tids.begin(); tidsIterator != tids.end();) {
            const int tid = *tidsIterator;

            // This assumes that group leader is added first
            const int groupLeaderFd = (pinned || notInGroup) ? -1 : *(eventIndexToTidToFdMap.at(0).at(tid));

            // must always pin them when not system wide as cannot read counters via group
            if (!sharedConfig.perfConfig.is_system_wide) {
                event.attr.pinned = 1;
            }

            lib::AutoClosingFd fd;

            // try with exclude_kernel clear
            {
                // clear
                event.attr.exclude_kernel = 0;
                event.attr.exclude_hv = 0;
                event.attr.exclude_idle = 0;

                // open event
                fd = sys_perf_event_open(&event.attr, tid, cpu, groupLeaderFd,
                // This is "(broken since Linux 2.6.35)" so can possibly be removed
                // we use PERF_EVENT_IOC_SET_OUTPUT anyway
                                         PERF_FLAG_FD_OUTPUT);
            }

            // retry with just exclude_kernel set
            if ((!fd) && (errno == EACCES)) {
                logg.logMessage("Failed when exclude_kernel == 0, retrying with exclude_kernel = 1");

                // set
                event.attr.exclude_kernel = 1;
                event.attr.exclude_hv = 0;
                event.attr.exclude_idle = 0;

                // open event
                fd = sys_perf_event_open(&event.attr, tid, cpu, groupLeaderFd,
                // This is "(broken since Linux 2.6.35)" so can possibly be removed
                // we use PERF_EVENT_IOC_SET_OUTPUT anyway
                                         PERF_FLAG_FD_OUTPUT);

                // retry with exclude_kernel and all set
                if ((!fd) && (errno == EACCES)) {
                    logg.logMessage("Failed when exclude_kernel == 1, exclude_hv == 0, exclude_idle == 0, retrying with all exclusions enabled");

                    // set
                    event.attr.exclude_kernel = 1;
                    event.attr.exclude_hv = 1;
                    event.attr.exclude_idle = 1;

                    // open event
                    fd = sys_perf_event_open(&event.attr, tid, cpu, groupLeaderFd,
                    // This is "(broken since Linux 2.6.35)" so can possibly be removed
                    // we use PERF_EVENT_IOC_SET_OUTPUT anyway
                                             PERF_FLAG_FD_OUTPUT);
                }
            }

            // restore pinned flag which is used to indicate leader
            event.attr.pinned = (pinned ? 1 : 0);

            logg.logMessage("perf_event_open: tid: %i, leader = %i -> fd = %i", tid, groupLeaderFd, *fd);

            if (!fd) {
                logg.logMessage("failed (%d) %s", errno, strerror(errno));

                if (errno == ENODEV) {
                    // The core is offline
                    return OnlineResult::CPU_OFFLINE;
                }
                else if (errno == ESRCH) {
                    // thread exited before we had chance to open event
                    tidsIterator = tids.erase(tidsIterator);
                    continue;
                }
                else if ((errno == ENOENT) && (!event.attr.pinned)) {
                    // This event doesn't apply to this CPU but should apply to a different one, e.g. bigLittle
                    goto skipOtherTids;
                }

                logg.logWarning("perf_event_open failed to online counter %s:0x%llx on CPU %d, due to errno=%d ('%s')", typeLabel, event.attr.config, cpu, errno, strerror(errno));

                if (sharedConfig.perfConfig.is_system_wide)
                    return OnlineResult::FAILURE;
            }
            else if (!perfBuffer->useFd(*fd, cpu)) {
                logg.logMessage("PerfBuffer::useFd failed");
                if (sharedConfig.perfConfig.is_system_wide)
                    return OnlineResult::FAILURE;
            }
            else if (!monitor->add(*fd)) {
                logg.logMessage("Monitor::add failed");
                return OnlineResult::FAILURE;
            }
            else {
                eventIndexToTidToFdMap[eventIndex][tid] = std::move(fd);
            }

            ++tidsIterator;
        }
skipOtherTids: ;
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

                coreKeys.push_back(key);
                ids.emplace_back(); // allocate now, set it next
                if (ioctl(*fd, PERF_EVENT_IOC_ID, &ids.back()) != 0 &&
                // Workaround for running 32-bit gatord on 64-bit systems, kernel patch in the works
                        ioctl(*fd, (PERF_EVENT_IOC_ID & ~IOCSIZE_MASK) | (8 << _IOC_SIZESHIFT), &ids.back()) != 0) {
                    logg.logMessage("ioctl failed");
                    return OnlineResult::OTHER_FAILURE;
                }
                addedEvents = true;
            }
        }

        if (!addedEvents) {
            logg.logMessage("no events came online");
        }

        buffer->marshalKeys(timestamp, ids.size(), ids.data(), coreKeys.data());
    }
    else {
        std::vector<int> keysInGroup;

        // send the ungrouped attributes, collect keys for grouped attributes
        for (const auto & eventIndexToTidToFdPair : eventIndexToTidToFdMap) {
            const int eventIndex = eventIndexToTidToFdPair.first;
            const PerfEvent & event = events.at(eventIndex);
            const bool notInGroup = neverInGroup(eventIndex);
            const bool isLeader = requiresLeader() && (eventIndex == 0);

            if ((event.attr.pinned || notInGroup) && !isLeader) {
                for (const auto & tidToFdPair : eventIndexToTidToFdPair.second) {
                    const auto & fd = tidToFdPair.second;
                    if (!readAndSend(timestamp, buffer, event.attr, *fd, 1, &event.key)) {
                        return OnlineResult::OTHER_FAILURE;
                    }
                }
            }
            else {
                keysInGroup.push_back(event.key);
            }
        }

        assert(((!requiresLeader()) || keysInGroup.empty()) && "Cannot read group items without leader");

        // send the grouped attributes and their keys
        if (!keysInGroup.empty()) {
            const auto & event = events.at(0);
            const auto & tidToFdMap = eventIndexToTidToFdMap.at(0);
            for (const auto & tidToFdPair : tidToFdMap) {
                const auto & fd = tidToFdPair.second;
                if (!readAndSend(timestamp, buffer, event.attr, *fd, keysInGroup.size(), keysInGroup.data())) {
                    return OnlineResult::OTHER_FAILURE;
                }
            }
        }
    }

    if (enableNow) {
        if (!enable(eventIndexToTidToFdMap) || !checkEnabled(eventIndexToTidToFdMap)) {
            return OnlineResult::OTHER_FAILURE;
        }
    }

    // everything enabled successfully, move into map
    cpuToEventIndexToTidToFdMap[cpu] = std::move(eventIndexToTidToFdMap);

    return OnlineResult::SUCCESS;
}

bool PerfEventGroup::offlineCPU(int cpu, PerfBuffer * perfBuffer)
{
    auto & eventIndexToTidToFdMap = cpuToEventIndexToTidToFdMap[cpu];

    // we disable in the opposite order that we enabled for some reason
    const auto eventIndexToTidToFdRend = eventIndexToTidToFdMap.rend();
    for (auto eventIndexToTidToFdIt = eventIndexToTidToFdMap.rbegin(); eventIndexToTidToFdIt != eventIndexToTidToFdRend; ++eventIndexToTidToFdIt) {
        const auto & tidToFdMap = eventIndexToTidToFdIt->second;
        const auto tidToFdRend = tidToFdMap.rend();
        for (auto tidToFdIt = tidToFdMap.rbegin(); tidToFdIt != tidToFdRend; ++tidToFdIt) {
            const auto & fd = tidToFdIt->second;
            if (ioctl(*fd, PERF_EVENT_IOC_DISABLE, 0) != 0) {
                logg.logMessage("ioctl failed");
                return false;
            }
        }
    }

    // Mark the buffer so that it will be released next time it's read
    perfBuffer->discard(cpu);

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
        const bool notInGroup = neverInGroup(eventIndex);

        for (const auto & tidToFdPair : eventIndexToTidToFdPair.second) {
            const auto & fd = tidToFdPair.second;

            if ((event.attr.pinned || notInGroup) && (ioctl(*fd, PERF_EVENT_IOC_ENABLE, 0) != 0)) {
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

    // Enable group leaders, others should be enabled by default
    for (const auto & eventIndexToTidToFdPair : eventIndexToTidToFdMap) {
        const int eventIndex = eventIndexToTidToFdPair.first;
        const PerfEvent & event = events.at(eventIndex);
        const bool notInGroup = neverInGroup(eventIndex);

        for (const auto & tidToFdPair : eventIndexToTidToFdPair.second) {
            const auto & fd = tidToFdPair.second;

            if ((event.attr.pinned || notInGroup) && (read(*fd, buf, sizeof(buf)) <= 0)) {
                logg.logError("Unable to read all perf groups, perhaps too many events were enabled");
                return false;
            }
        }
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
        for (auto eventIndexToTidToFdIt = eventIndexToTidToFdMap.rbegin(); eventIndexToTidToFdIt != eventIndexToTidToFdRend; ++eventIndexToTidToFdIt) {
            const auto & tidToFdMap = eventIndexToTidToFdIt->second;
            const auto tidToFdRend = tidToFdMap.rend();
            for (auto tidToFdIt = tidToFdMap.rbegin(); tidToFdIt != tidToFdRend; ++tidToFdIt) {
                const auto & fd = tidToFdIt->second;
                ioctl(*fd, PERF_EVENT_IOC_DISABLE, 0);
            }
        }
    }
}

int PerfEventGroup::nextDummyKey()
{
    return sharedConfig.dummyKeyCounter--;
}
