/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#include "lib/Assert.h"
#include "lib/FsEntry.h"
#include "non_root/ProcessStateTracker.h"
#include "non_root/ProcessStateChangeHandler.h"
#include "linux/proc/ProcPidStatFileRecord.h"
#include "linux/proc/ProcPidStatmFileRecord.h"

#include <algorithm>

#include <iostream>

namespace non_root
{
    namespace
    {
        static inline unsigned long long convertClkTicksToNS(unsigned long long ticks,
                                                             unsigned long long bootTimeBaseNS, unsigned long divider)
        {
            return std::max<long long>((ticks * (1e9 / divider)) - bootTimeBaseNS, 0);
        }
    }

    ProcessStateTracker::ActiveScan::ActiveScan(ProcessStateTracker & parent_, unsigned long long timestampNS_)
            : accumulatedTimePerCore(),
              parent(parent_),
              timestampNS(timestampNS_)
    {
    }

    ProcessStateTracker::ActiveScan::~ActiveScan()
    {
        parent.endScan(*this, accumulatedTimePerCore);
    }

    void ProcessStateTracker::ActiveScan::addProcess(int pid, int tid, const lnx::ProcPidStatFileRecord & statRecord,
                                                     const lib::Optional<lnx::ProcPidStatmFileRecord> & statmRecord,
                                                     const lib::Optional<lib::FsEntry> & exe)
    {
        // forward to parent
        const unsigned long long processTimeDelta = parent.add(timestampNS, pid, tid, statRecord, statmRecord, exe);

        // update time accumulation
        accumulatedTimePerCore[statRecord.getProcessor()] += processTimeDelta;
    }

    ProcessStateTracker::ProcessInfo::ProcessInfo()
            : statsTracker(0, 0, 0),
              startTimeNS(0),
              parentPid(PARENT_PID_UNKNOWN),
              state(State::EMPTY)
    {
    }

    ProcessStateTracker::ProcessInfo::ProcessInfo(int pid_, int tid_, unsigned long pageSize_, unsigned long long timestampNS_)
            : statsTracker(pid_, tid_, pageSize_),
              startTimeNS(timestampNS_),
              parentPid(PARENT_PID_UNKNOWN),
              state(State::NEW)
    {
    }

    ProcessStateTracker::ProcessInfo::ProcessInfo(ProcessInfo && that)
            : statsTracker(std::move(that.statsTracker)),
              startTimeNS(that.startTimeNS),
              parentPid(that.parentPid),
              state(that.state)
    {
        that.parentPid = PARENT_PID_UNKNOWN;
        that.state = State::EMPTY;
    }

    ProcessStateTracker::ProcessInfo& ProcessStateTracker::ProcessInfo::operator=(ProcessInfo && that)
    {
        this->statsTracker = std::move(that.statsTracker);
        this->startTimeNS = that.startTimeNS;
        this->parentPid = that.parentPid;
        this->state = that.state;

        that.parentPid = PARENT_PID_UNKNOWN;
        that.state = State::EMPTY;

        return *this;
    }

    void ProcessStateTracker::ProcessInfo::setSeenSinceLastScan(bool seen)
    {
        if (seen) {
            if (state == State::UNSEEN) {
                state = State::SEEN;
            }
        }
        else {
            if ((state == State::SEEN) || (state == State::NEW)) {
                state = State::UNSEEN;
            }
        }
    }

    unsigned long long ProcessStateTracker::ProcessInfo::update(
            unsigned long long bootTimeBaseNS, unsigned long clktck, const lnx::ProcPidStatFileRecord & statRecord,
            const lib::Optional<lnx::ProcPidStatmFileRecord> & statmRecord, const lib::Optional<lib::FsEntry> & exe)
    {
        if (isNew()) {
            // pull out parent pid
            parentPid = statRecord.getPpid();
            // update start time
            startTimeNS = convertClkTicksToNS(statRecord.getStarttime(), bootTimeBaseNS, clktck);
        }

        statsTracker.updateFromProcPidStatFileRecord(statRecord);

        if (statmRecord) {
            statsTracker.updateFromProcPidStatmFileRecord(*statmRecord);
        }

        if (exe) {
            statsTracker.updateExe(*exe);
        }

        return (!isNew() ? statsTracker.getTimeRunningDelta() : 0);
    }

    bool ProcessStateTracker::ProcessInfo::hasExitedAndRestartedSince(unsigned long long bootTimeBaseNS,
                                                                      unsigned long clktck, int pid, int tid,
                                                                      const lnx::ProcPidStatFileRecord & record)
    {

        if (!isNew()) {
            // if the pid has changed, then assume exited and then new process started that reused TID
            const int pidToCompare = (record.getPgid() != 0 ? pid : 0);
            if ((this->getPid() != pidToCompare) || (this->getTid() != tid)) {
                return true;
            }

            // if start time changes then assume reused TID
            const unsigned long long recordStartTimeNS = convertClkTicksToNS(record.getStarttime(), bootTimeBaseNS,
                                                                             clktck);
            if (this->startTimeNS != recordStartTimeNS) {
                return true;
            }

            // if parent pid is different then assume reused TID
            if ((parentPid != PARENT_PID_UNKNOWN) && (parentPid != record.getPpid())) {
                return true;
            }
        }

        return false;
    }

    void ProcessStateTracker::ProcessInfo::sendStats(unsigned long long timestampNS,
                                                     ProcessStateChangeHandler & handler, bool sendFakeSchedulingEvents)
    {
        statsTracker.sendStats(timestampNS, handler, sendFakeSchedulingEvents);
    }

    ProcessStateTracker::ProcessStateTracker(ProcessStateChangeHandler & handler_, unsigned long long bootTimeBaseNS_,
                                             unsigned long clktck_, unsigned long pageSize_)
            : handler(handler_),
              lastTimestampNS(0),
              bootTimeBaseNS(bootTimeBaseNS_),
              clktck(clktck_),
              pageSize(pageSize_),
              trackedProcesses(),
              firstIteration(true)
    {
    }

    std::unique_ptr<ProcessStateTracker::ActiveScan> ProcessStateTracker::beginScan(unsigned long long timestampNS)
    {
        return std::unique_ptr<ProcessStateTracker::ActiveScan>(new ActiveScan(*this, timestampNS));
    }

    void ProcessStateTracker::replaceProcessInfo(unsigned long long timestampNS,
                                                 const lnx::ProcPidStatFileRecord & statRecord,
                                                 ProcessInfo & processInfo)
    {
        const unsigned long long newStartTimestampNS = convertClkTicksToNS(statRecord.getStarttime(), bootTimeBaseNS,
                                                                           clktck);
        const unsigned long long timestampToUse = std::min(timestampNS, newStartTimestampNS - 1);

        // send exit event
        sendProcessExit(timestampToUse, processInfo);

        // create new
        processInfo = ProcessInfo(statRecord.getPgid(), statRecord.getPid(), pageSize, newStartTimestampNS);
    }

    unsigned long long ProcessStateTracker::add(unsigned long long timestampNS, int pid, int tid,
                                                const lnx::ProcPidStatFileRecord & statRecord,
                                                const lib::Optional<lnx::ProcPidStatmFileRecord> & statmRecord,
                                                const lib::Optional<lib::FsEntry> & exe)
    {
        runtime_assert(statRecord.getPid() == tid, "Record does not match tid");

        auto & processInfo = getProcessInfoFor(timestampNS, statRecord.getPgid(), pid, tid);

        if (processInfo.hasExitedAndRestartedSince(bootTimeBaseNS, clktck, pid, tid, statRecord)) {
            replaceProcessInfo(timestampNS, statRecord, processInfo);
        }
        else {
            processInfo.setSeenSinceLastScan(true);
        }

        return processInfo.update(bootTimeBaseNS, clktck, statRecord, statmRecord, exe);
    }

    /**
     * Generates events into the capture buffer based on the changes detected in the scan.
     * This process will:
     *   - send new-process events
     *   - update the various per-process counters
     *   - send ended-process events
     *   - generate *fake* scheduling data
     *
     * The process of generating fake scheduling data envolves (for each core) calculating the number of ticks spend by each process running (utime and stime deltas since last scan)
     * and then allocating a proportion of the total number of ticks for that core to the process. The proportionate amount of time spent on each process is used to generate fake timestamps
     * between the last scan and the current scan (as well as possibly idle time) this is then used to emit sched switch events.
     * This is obviously incorrect with respect to the actual scheduling of processes on the system, but since it is not possible to observe the actual scheduling events, this at least allows Streamline
     * to display an approximately correct heatmap and core map view.
     */
    void ProcessStateTracker::endScan(
            const ActiveScan & activeScan,
            const std::map<unsigned long, unsigned long long> & accumulatedTimePerCore)
    {
        runtime_assert(firstIteration || (activeScan.timestampNS > lastTimestampNS), "timestampNS <= lastTimestampNS");

        const unsigned long long scanDurationNS = (!firstIteration ? activeScan.timestampNS - lastTimestampNS : 0);

        std::map<unsigned long, unsigned long long> relativeTimestampMap;
        std::map<unsigned long, double> coreRunningTimeMultiplier;
        std::map<unsigned long, double> coreTotalTimeMultiplier;

        const double hzToNs = (1e9 / clktck);
        for (const auto & entry : accumulatedTimePerCore) {
            if (entry.second > 0) {
                const unsigned long long coreDurationTicks = entry.second;
                const unsigned long long coreDurationNs = coreDurationTicks * hzToNs;
                const double multiplier = scanDurationNS / double(entry.second);

                coreTotalTimeMultiplier[entry.first] = multiplier;

                if (coreDurationNs < scanDurationNS) {
                    // lest time spent on core than in scan...
                    // convert direct from ticks to ns, so that any remaining time is allocated to an idle gap
                    // so the capture will end up "[PROCESS..][IDLE][PROCESS.....][IDLE...]..."
                    coreRunningTimeMultiplier[entry.first] = hzToNs;
                }
                else {
                    // somehow the value is bigger than expected
                    // scale ticks down accoringly
                    // there will be no idles inserted
                    coreRunningTimeMultiplier[entry.first] = multiplier;
                }
            }
            else {
                coreRunningTimeMultiplier[entry.first] = 0;
                coreTotalTimeMultiplier[entry.first] = 0;
            }
        }

        // iterate over all entries in the trackedProcesses map; if an entry is marked seen, then just emit any state changes
        // and mark it as unseen ready for the next scan, otherwise remove it and send the process ended state change
        auto iterator = trackedProcesses.begin();
        const auto end = trackedProcesses.end();
        while (iterator != end) {
            auto & processInfo = iterator->second;
            if (processInfo.isSeenSinceLastScan()) {
                // send new event if required
                if (processInfo.isNew()) {
                    handler.onNewProcess(processInfo.getStartTimeNS(), processInfo.getProcessor(),
                                         processInfo.getParentPid(), processInfo.getPid(), processInfo.getTid(),
                                         processInfo.getComm(), processInfo.getExePath());
                }

                // number of ticks process was running for since last scan; used to emulate time spent running on processor for fake scheduling events
                const unsigned long long processRunningTime = processInfo.getTimeRunningDelta();

                // whether or not to fake scheduling events for process
                bool shouldSendSchedEvent = (!firstIteration) && (!processInfo.isNew())
                        && (processRunningTime > 0);

                // calculate fake timestamp for process
                unsigned long long & relativeTimestampEntryRef = relativeTimestampMap[processInfo.getProcessor()];
                const unsigned long long fakeTimestampNS = relativeTimestampEntryRef + lastTimestampNS;
                const unsigned long long totalGapTimeNs = coreTotalTimeMultiplier[processInfo.getProcessor()] * processRunningTime;
                const unsigned long long fakeRunningTimeNS = coreRunningTimeMultiplier[processInfo.getProcessor()] * processRunningTime;
                // update fake timestamp tracker for core by some relative fraction of overall ticks
                if (shouldSendSchedEvent) {
                    relativeTimestampEntryRef += std::max(fakeRunningTimeNS, totalGapTimeNs);
                    if (fakeRunningTimeNS == 0) {
                        // dont send it if the amount of time is so small as be rounded to zero
                        shouldSendSchedEvent = false;
                    }
                }

                // send updates to state
                processInfo.sendStats((shouldSendSchedEvent ? fakeTimestampNS : activeScan.timestampNS), handler,
                                      shouldSendSchedEvent);

                // mark it as unseen for next pass
                processInfo.setSeenSinceLastScan(false);

                // send idle time
                if (shouldSendSchedEvent && (fakeRunningTimeNS < totalGapTimeNs)) {
                    handler.idle(fakeTimestampNS + fakeRunningTimeNS, processInfo.getProcessor());
                }

                ++iterator;
            }
            else {
                if (!processInfo.isEmpty()) {
                    // send process exit state change
                    sendProcessExit(activeScan.timestampNS, processInfo);
                }

                // remove from map
                iterator = trackedProcesses.erase(iterator);
            }
        }

        // clear first iteration flag and save last value of timestampNS
        firstIteration = false;
        lastTimestampNS = activeScan.timestampNS;
    }

    ProcessStateTracker::ProcessInfo & ProcessStateTracker::getProcessInfoFor(unsigned long long timestampNS, int pgid,
                                                                              int pid, int tid)
    {
        const int pidToUse = (pgid != 0 ? pid : 0); // kernel threads have pgid 0, and pid == tid, change their pid to 0
        auto & processInfo = trackedProcesses[tid];

        if (processInfo.isEmpty()) {
            // create new record - the new item event is send in endScan
            processInfo = ProcessInfo(pidToUse, tid, pageSize, timestampNS);
        }

        return processInfo;
    }

    void ProcessStateTracker::sendProcessExit(unsigned long long timestampNS, ProcessInfo & processInfo)
    {
        handler.onExitProcess(timestampNS, processInfo.getProcessor(), processInfo.getTid());
    }
}
