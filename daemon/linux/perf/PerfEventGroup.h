/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_PERF_PERF_EVENT_GROUP_H
#define INCLUDE_LINUX_PERF_PERF_EVENT_GROUP_H

#include <cstdint>
#include <climits>
#include <map>
#include <set>
#include <vector>

#include "ClassBoilerPlate.h"
#include "k/perf_event.h" // Use a snapshot of perf_event.h as it may be more recent than what is on the target and if not newer features won't be supported anyways
#include "lib/AutoClosingFd.h"
#include "linux/perf/PerfConfig.h"
#include "linux/perf/PerfEventGroupIdentifier.h"

class Buffer;
class GatorCpu;
class Monitor;
class PerfBuffer;

enum PerfGroupFlags
{
    PERF_GROUP_MMAP = 1 << 0,
    PERF_GROUP_COMM = 1 << 1,
    PERF_GROUP_FREQ = 1 << 2,
    PERF_GROUP_TASK = 1 << 3,
    PERF_GROUP_SAMPLE_ID_ALL = 1 << 4,
    PERF_GROUP_CONTEXT_SWITCH = 1 << 5,
};

enum class OnlineResult
{
    SUCCESS,
    FAILURE,
    CPU_OFFLINE,
    OTHER_FAILURE,
};

struct PerfEventGroupSharedConfig
{
    inline PerfEventGroupSharedConfig(const PerfConfig & perfConfig)
            : perfConfig(perfConfig),
              schedSwitchId(-1),
              dummyKeyCounter(INT_MAX)
    {
    }

    const PerfConfig & perfConfig;
    int schedSwitchId;
    int dummyKeyCounter;
};

class PerfEventGroup
{
public:

    PerfEventGroup(const PerfEventGroupIdentifier & groupIdentifier, PerfEventGroupSharedConfig & sharedConfig);

    bool requiresLeader() const;
    bool hasLeader() const;
    bool addEvent(bool leader, uint64_t timestamp, Buffer * buffer, int key, uint32_t type, uint64_t config, uint64_t frequencyOrPeriod, uint64_t sampleType,
                  int flags);
    bool createGroupLeader(uint64_t timestamp, Buffer * buffer);

    OnlineResult onlineCPU(uint64_t timestamp, int cpu, std::set<int> & tids, bool enableNow, Buffer * buffer, Monitor * monitor, PerfBuffer * perfBuffer);

    bool offlineCPU(int cpu, PerfBuffer * perfBuffer);
    void start();
    void stop();

private:

    struct PerfEvent
    {
        struct perf_event_attr attr;
        int flags;
        int key;
    };

    CLASS_DELETE_COPY_MOVE(PerfEventGroup);

    bool createCpuGroupLeader(uint64_t timestamp, Buffer * buffer);
    bool createUncoreGroupLeader(uint64_t timestamp, Buffer * buffer);

    bool neverInGroup(int eventIndex) const;
    bool enable(const std::map<int, std::map<int, lib::AutoClosingFd>> & eventIndexToTidToFdMap);
    bool checkEnabled(const std::map<int, std::map<int, lib::AutoClosingFd>> & eventIndexToTidToFdMap);

    int nextDummyKey();

    const PerfEventGroupIdentifier groupIdentifier;
    PerfEventGroupSharedConfig & sharedConfig;

    // list of events associated with the group, where the first must be the group leader
    std::vector<PerfEvent> events;

    // map from cpu -> (map from mEvents index -> (map from tid -> file descriptor))
    std::map<int, std::map<int, std::map<int, lib::AutoClosingFd>>> cpuToEventIndexToTidToFdMap;
};

#endif /* INCLUDE_LINUX_PERF_PERF_EVENT_GROUP_H */
