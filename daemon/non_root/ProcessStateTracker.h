/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_PROCESSSTATETRACKER_H
#define INCLUDE_NON_ROOT_PROCESSSTATETRACKER_H

#include "ClassBoilerPlate.h"
#include "lib/Optional.h"
#include "non_root/ProcessStatsTracker.h"

#include <map>
#include <memory>
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
     * Maintains a record of the current processes/threads and will generate appropriate events when these change
     */
    class ProcessStateTracker
    {
    public:

        /**
         * Tracks the state of a scan pass in ProcessPoller, once the scan is finished, updates its parent
         * ProcessStateTracker with changes since the last scan.
         *
         * This allows to determine the difference at each pass
         */
        class ActiveScan
        {
        public:

            ~ActiveScan();

            /**
             * Accept one /proc/[PID]/stat or /proc/[PID]/task/[TID]/stat record and an optional /proc/[PID]/statm or /proc/[PID]/task/[TID]/statm
             */
            void addProcess(int pid, int tid, const lnx::ProcPidStatFileRecord & statRecord,
                            const lib::Optional<lnx::ProcPidStatmFileRecord> & statmRecord,
                            const lib::Optional<lib::FsEntry> & exe);

        private:

            /** Only ProcessStateTracker can construct */
            friend class ProcessStateTracker;

            // sum up all time per core spent in system and user for all processes
            std::map<unsigned long, unsigned long long> accumulatedTimePerCore;
            ProcessStateTracker & parent;
            unsigned long long timestampNS;

            ActiveScan(ProcessStateTracker & parent, unsigned long long timestampNS);

            CLASS_DELETE_COPY_MOVE(ActiveScan);
        };

        /** Constructor */
        ProcessStateTracker(ProcessStateChangeHandler & handler, unsigned long long bootTimeBaseNS,
                            unsigned long clktck, unsigned long pageSize);

        /**
         * Begin a scan
         */
        std::unique_ptr<ProcessStateTracker::ActiveScan> beginScan(unsigned long long timestampNS);

    private:

        /**
         * State object for a given TID
         */
        class ProcessInfo
        {
        public:

            static constexpr const int PARENT_PID_UNKNOWN = -1;

            enum class State
            {
                EMPTY,
                NEW,
                SEEN,
                UNSEEN
            };

            ProcessInfo();
            ProcessInfo(int pid, int tid, unsigned long pageSize, unsigned long long timestampNS);
            ProcessInfo(ProcessInfo &&);
            ProcessInfo& operator=(ProcessInfo &&);

            bool isEmpty() const
            {
                return (state == State::EMPTY);
            }

            bool isNew() const
            {
                return (state == State::NEW);
            }

            bool isSeenSinceLastScan() const
            {
                return (state == State::NEW) || (state == State::SEEN);
            }

            int getPid() const
            {
                return statsTracker.getPid();
            }

            int getTid() const
            {
                return statsTracker.getTid();
            }

            unsigned long long getStartTimeNS() const
            {
                return startTimeNS;
            }

            unsigned long getProcessor() const
            {
                return statsTracker.getProcessor();
            }

            int getParentPid() const
            {
                // send 0 (i.e. the kernel) if PARENT_PID_UNKNOWN
                return parentPid != PARENT_PID_UNKNOWN ? parentPid : 0;
            }

            const std::string & getComm() const
            {
                return statsTracker.getComm();
            }

            const std::string & getExePath() const
            {
                return statsTracker.getExePath();
            }

            unsigned long long getTimeRunningDelta() const
            {
                return statsTracker.getTimeRunningDelta();
            }

            bool hasExitedAndRestartedSince(unsigned long long bootTimeBaseNS, unsigned long clktck, int pid, int tid,
                                            const lnx::ProcPidStatFileRecord & record);
            void setSeenSinceLastScan(bool seen);
            unsigned long long update(unsigned long long bootTimeBaseNS, unsigned long clktck,
                                      const lnx::ProcPidStatFileRecord & statRecord,
                                      const lib::Optional<lnx::ProcPidStatmFileRecord> & statmRecord,
                                      const lib::Optional<lib::FsEntry> & exe);
            void sendStats(unsigned long long timestampNS, ProcessStateChangeHandler & handler,
                           bool sendFakeSchedulingEvents);

        private:

            ProcessStatsTracker statsTracker;
            unsigned long long startTimeNS;
            int parentPid;
            State state;

            CLASS_DELETE_COPY(ProcessInfo);
        };

        /** Can call endScan */
        friend class ActiveScan;

        /** Receives state change events */
        ProcessStateChangeHandler & handler;

        /** Last value of timestampNS on previous run */
        unsigned long long lastTimestampNS;

        /** Base value for boot time based clk ticks to transform to monotonic time */
        unsigned long long bootTimeBaseNS;

        /** Clock tick multiplier - sysconf(_SC_CLK_TCK) */
        unsigned long clktck;

        /** Page size - sysconf(_SC_PAGESIZE) */
        unsigned long pageSize;

        /** Tracked processes map: TID->ProcessInfo */
        std::map<int, ProcessInfo> trackedProcesses;

        /** True only on the first time the scan runs */
        bool firstIteration;

        /**
         * Accept one /proc/[PID]/stat or /proc/[PID]/task/[TID]/stat record and optinally one /proc/[PID]/statm or /proc/[PID]/task/[TID]/statm record
         */
        unsigned long long add(unsigned long long timestampNS, int pid, int tid,
                               const lnx::ProcPidStatFileRecord & statRecord,
                               const lib::Optional<lnx::ProcPidStatmFileRecord> & statmRecord,
                               const lib::Optional<lib::FsEntry> & exe);

        /** Called when active scan is destructed to mutate state */
        void endScan(const ActiveScan & activeScan,
                     const std::map<unsigned long, unsigned long long> & accumulatedTimePerCore);

        /** Find the ProcessInformation object for some pid-tid pair */
        ProcessInfo & getProcessInfoFor(unsigned long long timestampNS, int pgid, int pid, int tid);

        /** Send process exited event for object */
        void sendProcessExit(unsigned long long timestampNS, ProcessInfo & processInfo);

        /** Replace existing process info with new record */
        void replaceProcessInfo(unsigned long long timestampNS, const lnx::ProcPidStatFileRecord & statRecord,
                                ProcessInfo & processInfo);
    };
}

#endif /* INCLUDE_NON_ROOT_PROCESSSTATETRACKER_H */
