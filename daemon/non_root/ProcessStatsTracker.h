/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_PROCESSSTATSTRACKER_H
#define INCLUDE_NON_ROOT_PROCESSSTATSTRACKER_H

#include "ClassBoilerPlate.h"
#include "non_root/CounterHelpers.h"
#include "non_root/ProcessCounter.h"

#include <algorithm>
#include <string>

namespace lib
{
    class FsEntry;
}

namespace lnx
{
    class ProcPidStatFileRecord;
    class ProcPidStatmFileRecord;
}

namespace non_root
{
    class ProcessStateChangeHandler;

    /**
     * Extracts and monitors interesting process stats from various sources such as ProcPidStatFileRecord and ProcPidStatmFileRecord
     */
    class ProcessStatsTracker
    {
    public:

        ProcessStatsTracker(int pid, int tid, unsigned long pageSize);

        CLASS_DEFAULT_COPY_MOVE(ProcessStatsTracker);

        const std::string & getComm() const
        {
            return comm.value();
        }

        const std::string & getExePath() const
        {
            return exe_path.value();
        }

        int getPid() const
        {
            return pid;
        }

        int getTid() const
        {
            return tid;
        }

        unsigned long getProcessor() const
        {
            return stat_processor.value();
        }

        unsigned long long getTimeRunningDelta() const
        {
            return std::max<long long>(stat_stime.delta(), 0) + std::max<long long>(stat_utime.delta(), 0);
        }

        void sendStats(unsigned long long timestampNS, ProcessStateChangeHandler & handler,
                       bool sendFakeSchedulingEvents);
        void updateFromProcPidStatFileRecord(const lnx::ProcPidStatFileRecord & record);
        void updateFromProcPidStatmFileRecord(const lnx::ProcPidStatmFileRecord & record);
        void updateExe(const lib::FsEntry & exe);

    private:

        AbsoluteCounter<std::string> comm;
        AbsoluteCounter<std::string> exe_path;
        DeltaCounter<unsigned long> stat_minflt;
        DeltaCounter<unsigned long> stat_majflt;
        DeltaCounter<unsigned long> stat_utime;
        DeltaCounter<unsigned long> stat_stime;
        DeltaCounter<unsigned long> stat_guest_time;
        AbsoluteCounter<unsigned long> stat_vsize;
        AbsoluteCounter<unsigned long> stat_rss;
        AbsoluteCounter<unsigned long> stat_rsslim;
        AbsoluteCounter<unsigned long> statm_shared;
        AbsoluteCounter<unsigned long> statm_text;
        AbsoluteCounter<unsigned long> statm_data;
        AbsoluteCounter<unsigned long> stat_processor;
        AbsoluteCounter<long> stat_num_threads;
        unsigned long pageSize;
        int pid;
        int tid;
        bool newProcess;

        template<typename T>
        void writeCounter(unsigned long long timestampNS, ProcessStateChangeHandler & handler,
                          AbsoluteProcessCounter id, AbsoluteCounter<T> & counter);

        template<typename T>
        void writeCounter(unsigned long long timestampNS, ProcessStateChangeHandler & handler, DeltaProcessCounter id,
                          DeltaCounter<T> & counter);
    };
}

#endif /* INCLUDE_NON_ROOT_PROCESSSTATSTRACKER_H */
