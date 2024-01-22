/* Copyright (C) 2017-2023 by Arm Limited. All rights reserved. */

#include "non_root/ProcessStatsTracker.h"

#include "linux/proc/ProcPidStatFileRecord.h"
#include "linux/proc/ProcPidStatmFileRecord.h"
#include "non_root/CounterHelpers.h"
#include "non_root/ProcessCounter.h"
#include "non_root/ProcessStateChangeHandler.h"

#include <string>

namespace non_root {
    ProcessStatsTracker::ProcessStatsTracker(int pid_, int tid_, unsigned long pageSize_)
        : pageSize(pageSize_), pid(pid_), tid(tid_)

    {
    }

    void ProcessStatsTracker::sendStats(unsigned long long timestampNS,
                                        ProcessStateChangeHandler & handler,
                                        bool sendFakeSchedulingEvents)
    {
        // send process activity values (time spent in userspace, kernel space and the last seen processor
        if (sendFakeSchedulingEvents) {
            handler.threadActivity(timestampNS, tid, stat_utime.delta(), stat_stime.delta(), stat_processor.value());
        }

        // send changed COMM
        if (comm.changed()) {
            if (!newProcess) {
                handler.onCommChange(timestampNS, getProcessor(), tid, comm.value());
            }
            comm.done();
        }

        // send changed EXE value
        if (exe_path.changed()) {
            if (!newProcess) {
                handler.onExeChange(timestampNS, getProcessor(), pid, tid, exe_path.value());
            }
            exe_path.done();
        }

        // send counters
        writeCounter(timestampNS, handler, AbsoluteProcessCounter::DATA_SIZE, statm_data);
        writeCounter(timestampNS, handler, AbsoluteProcessCounter::NUM_THREADS, stat_num_threads);
        writeCounter(timestampNS, handler, AbsoluteProcessCounter::RES_LIMIT, stat_rsslim);
        writeCounter(timestampNS, handler, AbsoluteProcessCounter::RES_SIZE, stat_rss);
        writeCounter(timestampNS, handler, AbsoluteProcessCounter::SHARED_SIZE, statm_shared);
        writeCounter(timestampNS, handler, AbsoluteProcessCounter::TEXT_SIZE, statm_text);
        writeCounter(timestampNS, handler, AbsoluteProcessCounter::VM_SIZE, stat_vsize);
        writeCounter(timestampNS, handler, DeltaProcessCounter::MINOR_FAULTS, stat_minflt);
        writeCounter(timestampNS, handler, DeltaProcessCounter::MAJOR_FAULTS, stat_majflt);
        writeCounter(timestampNS, handler, DeltaProcessCounter::UTIME, stat_utime);
        writeCounter(timestampNS, handler, DeltaProcessCounter::STIME, stat_stime);
        writeCounter(timestampNS, handler, DeltaProcessCounter::GUEST_TIME, stat_guest_time);

        newProcess = false;
    }

    void ProcessStatsTracker::updateFromProcPidStatFileRecord(const lnx::ProcPidStatFileRecord & record)
    {
        comm.update(record.getComm());
        stat_minflt.update(record.getMinflt());
        stat_majflt.update(record.getMajflt());
        stat_utime.update(record.getUtime());
        stat_stime.update(record.getStime());
        stat_guest_time.update(record.getGuestTime());
        stat_num_threads.update(record.getNumThreads());
        stat_vsize.update(record.getVsize());
        stat_rss.update(record.getRss() * pageSize);
        stat_rsslim.update(record.getRsslim());
        stat_processor.update(record.getProcessor());
    }

    void ProcessStatsTracker::updateFromProcPidStatmFileRecord(const lnx::ProcPidStatmFileRecord & record)
    {
        statm_shared.update(record.getShared() * pageSize);
        statm_text.update(record.getText() * pageSize);
        statm_data.update(record.getData() * pageSize);
    }

    void ProcessStatsTracker::updateExe(const std::string & exe)
    {
        exe_path.update(exe);
    }

    template<typename T>
    void ProcessStatsTracker::writeCounter(unsigned long long timestampNS,
                                           ProcessStateChangeHandler & handler,
                                           AbsoluteProcessCounter id,
                                           AbsoluteCounter<T> & counter)
    {
        handler.absoluteCounter(timestampNS, getProcessor(), tid, id, counter.value());
        counter.done();
    }

    template<typename T>
    void ProcessStatsTracker::writeCounter(unsigned long long timestampNS,
                                           ProcessStateChangeHandler & handler,
                                           DeltaProcessCounter id,
                                           DeltaCounter<T> & counter)
    {
        // send zero for first event to avoid potential big spike
        handler.deltaCounter(timestampNS, getProcessor(), tid, id, newProcess ? 0 : counter.delta());
        counter.done();
    }
}
