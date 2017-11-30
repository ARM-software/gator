/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#include "non_root/GlobalStatsTracker.h"
#include "non_root/GlobalStateChangeHandler.h"

namespace non_root
{
    GlobalStatsTracker::PerCoreStatsTracker::PerCoreStatsTracker()
            : timeUserTicks(),
              timeNiceTicks(),
              timeSystemTicks(),
              timeIdleTicks(),
              timeIowaitTicks(),
              timeIrqTicks(),
              timeSoftirqTicks(),
              timeStealTicks(),
              timeGuestTicks(),
              timeGuestNiceTicks(),
              first(true)
    {
    }

    void GlobalStatsTracker::PerCoreStatsTracker::sendStats(unsigned long long timestampNS,
                                                            GlobalStateChangeHandler & handler, unsigned long cpuID)
    {
        writeCounter(timestampNS, handler, cpuID, DeltaGlobalCounter::TIME_CPU_USER, timeUserTicks);
        writeCounter(timestampNS, handler, cpuID, DeltaGlobalCounter::TIME_CPU_NICE, timeNiceTicks);
        writeCounter(timestampNS, handler, cpuID, DeltaGlobalCounter::TIME_CPU_SYSTEM, timeSystemTicks);
        writeCounter(timestampNS, handler, cpuID, DeltaGlobalCounter::TIME_CPU_IDLE, timeIdleTicks);
        writeCounter(timestampNS, handler, cpuID, DeltaGlobalCounter::TIME_CPU_IOWAIT, timeIowaitTicks);
        writeCounter(timestampNS, handler, cpuID, DeltaGlobalCounter::TIME_CPU_IRQ, timeIrqTicks);
        writeCounter(timestampNS, handler, cpuID, DeltaGlobalCounter::TIME_CPU_SOFTIRQ, timeSoftirqTicks);
        writeCounter(timestampNS, handler, cpuID, DeltaGlobalCounter::TIME_CPU_STEAL, timeStealTicks);
        writeCounter(timestampNS, handler, cpuID, DeltaGlobalCounter::TIME_CPU_GUEST, timeGuestTicks);
        writeCounter(timestampNS, handler, cpuID, DeltaGlobalCounter::TIME_CPU_GUEST_NICE, timeGuestNiceTicks);

        first = false;
    }

    void GlobalStatsTracker::PerCoreStatsTracker::updateFromProcStatFileRecordCpuTime(
            const lnx::ProcStatFileRecord::CpuTime & record)
    {
        timeUserTicks.update(record.user_ticks);
        timeNiceTicks.update(record.nice_ticks);
        timeSystemTicks.update(record.system_ticks);
        timeIdleTicks.update(record.idle_ticks);
        timeIowaitTicks.update(record.iowait_ticks);
        timeIrqTicks.update(record.irq_ticks);
        timeSoftirqTicks.update(record.softirq_ticks);
        timeStealTicks.update(record.steal_ticks);
        timeGuestTicks.update(record.guest_ticks);
        timeGuestNiceTicks.update(record.guest_nice_ticks);
    }

    template<typename T>
    void GlobalStatsTracker::PerCoreStatsTracker::writeCounter(unsigned long long timestampNS,
                                                               GlobalStateChangeHandler & handler, unsigned long cpuID,
                                                               DeltaGlobalCounter id, DeltaCounter<T> & counter)
    {
        // send zero for first event to avoid potential big spike
        handler.deltaCounter(timestampNS, cpuID, id, first ? 0 : counter.delta());
        counter.done();
    }

    GlobalStatsTracker::GlobalStatsTracker(GlobalStateChangeHandler & handler_)
            : perCoreStats(),
              loadavgOver1Minute(),
              loadavgOver5Minutes(),
              loadavgOver15Minutes(),
              numProcessesRunning(),
              numProcessesExist(),
              numContextSwitchs(),
              numIrq(),
              numSoftIrq(),
              numForks(),
              handler(handler_),
              first(true)
    {
    }

    void GlobalStatsTracker::sendStats(unsigned long long timestampNS)
    {
        writeCounter(timestampNS, AbsoluteGlobalCounter::LOADAVG_1_MINUTE, loadavgOver1Minute);
        writeCounter(timestampNS, AbsoluteGlobalCounter::LOADAVG_5_MINUTES, loadavgOver5Minutes);
        writeCounter(timestampNS, AbsoluteGlobalCounter::LOADAVG_15_MINUTES, loadavgOver15Minutes);
        writeCounter(timestampNS, AbsoluteGlobalCounter::NUM_PROCESSES_RUNNING, numProcessesRunning);
        writeCounter(timestampNS, AbsoluteGlobalCounter::NUM_PROCESSES_EXISTING, numProcessesExist);
        writeCounter(timestampNS, DeltaGlobalCounter::NUM_CONTEXT_SWITCHES, numContextSwitchs);
        writeCounter(timestampNS, DeltaGlobalCounter::NUM_IRQ, numIrq);
        writeCounter(timestampNS, DeltaGlobalCounter::NUM_SOFTIRQ, numSoftIrq);
        writeCounter(timestampNS, DeltaGlobalCounter::NUM_FORKS, numForks);

        const bool oneOneCoreStatsEntry = (perCoreStats.size() == 1);
        for (auto & perCoreEntry : perCoreStats) {
            if (oneOneCoreStatsEntry || (perCoreEntry.first != lnx::ProcStatFileRecord::GLOBAL_CPU_TIME_ID)) {
                perCoreEntry.second.sendStats(timestampNS, handler, perCoreEntry.first);
            }
        }

        first = false;
    }

    void GlobalStatsTracker::updateFromProcLoadAvgFileRecord(const lnx::ProcLoadAvgFileRecord & record)
    {
        loadavgOver1Minute.update(record.getLoadAvgOver1Minutes() * non_root::GlobalStatsTracker::LOADAVG_MULTIPLIER);
        loadavgOver5Minutes.update(record.getLoadAvgOver5Minutes() * non_root::GlobalStatsTracker::LOADAVG_MULTIPLIER);
        loadavgOver15Minutes.update(
                record.getLoadAvgOver15Minutes() * non_root::GlobalStatsTracker::LOADAVG_MULTIPLIER);
        numProcessesRunning.update(record.getNumRunnableThreads());
        numProcessesExist.update(record.getNumThreads());
    }

    void GlobalStatsTracker::updateFromProcStatFileRecord(const lnx::ProcStatFileRecord & record)
    {
        if (record.getCtxt()) {
            numContextSwitchs.update(*record.getCtxt());
        }
        if (record.getIntr()) {
            numIrq.update(*record.getIntr());
        }
        if (record.getSoftIrq()) {
            numSoftIrq.update(*record.getSoftIrq());
        }
        if (record.getProcesses()) {
            numForks.update(*record.getProcesses());
        }

        for (const auto & cpuTime : record.getCpus()) {
            perCoreStats[cpuTime.cpu_id].updateFromProcStatFileRecordCpuTime(cpuTime);
        }
    }

    template<typename T>
    void GlobalStatsTracker::writeCounter(unsigned long long timestampNS, AbsoluteGlobalCounter id,
                                          AbsoluteCounter<T> & counter)
    {
        handler.absoluteCounter(timestampNS, id, counter.value());
        counter.done();
    }

    template<typename T>
    void GlobalStatsTracker::writeCounter(unsigned long long timestampNS, DeltaGlobalCounter id,
                                          DeltaCounter<T> & counter)
    {
        // send zero for first event to avoid potential big spike
        handler.deltaCounter(timestampNS, id, first ? 0 : counter.delta());
        counter.done();
    }
}
