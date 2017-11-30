/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_GLOBALSTATSTRACKER_H
#define INCLUDE_NON_ROOT_GLOBALSTATSTRACKER_H

#include "ClassBoilerPlate.h"
#include "linux/proc/ProcLoadAvgFileRecord.h"
#include "linux/proc/ProcStatFileRecord.h"
#include "non_root/CounterHelpers.h"
#include "non_root/GlobalCounter.h"

#include <map>

namespace non_root
{
    class GlobalStateChangeHandler;

    /**
     * Extracts and monitors interesting global stats from various sources such as ProcLoadAvgFileRecord and ProcStatFileRecord
     */
    class GlobalStatsTracker
    {
    public:

        /**
         * Extracts and monitors intersting per-core stats from ProcStatFileRecord (CPU entries)
         */
        class PerCoreStatsTracker
        {
        public:

            PerCoreStatsTracker();CLASS_DEFAULT_COPY_MOVE(PerCoreStatsTracker)
            ;

            void sendStats(unsigned long long timestampNS, GlobalStateChangeHandler & handler, unsigned long cpuID);
            void updateFromProcStatFileRecordCpuTime(const lnx::ProcStatFileRecord::CpuTime & record);

        private:

            DeltaCounter<unsigned long long> timeUserTicks;
            DeltaCounter<unsigned long long> timeNiceTicks;
            DeltaCounter<unsigned long long> timeSystemTicks;
            DeltaCounter<unsigned long long> timeIdleTicks;
            DeltaCounter<unsigned long long> timeIowaitTicks;
            DeltaCounter<unsigned long long> timeIrqTicks;
            DeltaCounter<unsigned long long> timeSoftirqTicks;
            DeltaCounter<unsigned long long> timeStealTicks;
            DeltaCounter<unsigned long long> timeGuestTicks;
            DeltaCounter<unsigned long long> timeGuestNiceTicks;
            bool first;

            template<typename T>
            void writeCounter(unsigned long long timestampNS, GlobalStateChangeHandler & handler, unsigned long cpuID,
                              DeltaGlobalCounter id, DeltaCounter<T> & counter);
        };

        /* to convert loadavg values from double to unsigned long */
        static constexpr const unsigned long LOADAVG_MULTIPLIER = 100;

        GlobalStatsTracker(GlobalStateChangeHandler & handler);
        CLASS_DEFAULT_COPY_MOVE(GlobalStatsTracker);

        void sendStats(unsigned long long timestampNS);
        void updateFromProcLoadAvgFileRecord(const lnx::ProcLoadAvgFileRecord & record);
        void updateFromProcStatFileRecord(const lnx::ProcStatFileRecord & record);

    private:

        std::map<unsigned long, PerCoreStatsTracker> perCoreStats;
        AbsoluteCounter<unsigned long> loadavgOver1Minute;
        AbsoluteCounter<unsigned long> loadavgOver5Minutes;
        AbsoluteCounter<unsigned long> loadavgOver15Minutes;
        AbsoluteCounter<unsigned long> numProcessesRunning;
        AbsoluteCounter<unsigned long> numProcessesExist;
        DeltaCounter<unsigned long> numContextSwitchs;
        DeltaCounter<unsigned long> numIrq;
        DeltaCounter<unsigned long> numSoftIrq;
        DeltaCounter<unsigned long> numForks;
        GlobalStateChangeHandler & handler;
        bool first;

        template<typename T>
        void writeCounter(unsigned long long timestampNS, AbsoluteGlobalCounter id, AbsoluteCounter<T> & counter);

        template<typename T>
        void writeCounter(unsigned long long timestampNS, DeltaGlobalCounter id, DeltaCounter<T> & counter);
    };
}

#endif /* INCLUDE_NON_ROOT_GLOBALSTATSTRACKER_H */
