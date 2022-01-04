/* Copyright (C) 2017-2021 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_PROC_PROCESSPOLLERBASE_H
#define INCLUDE_LINUX_PROC_PROCESSPOLLERBASE_H

#include "lib/FsEntry.h"
#include "lib/TimestampSource.h"
#include "linux/proc/ProcPidStatFileRecord.h"
#include "linux/proc/ProcPidStatmFileRecord.h"

#include <optional>

namespace lnx {
    /**
     * Scans the contents of /proc/[PID]/stat, /proc/[PID]/statm, /proc/[PID]/task/[TID]/stat and /proc/[PID]/task/[TID]/statm files
     * passing the extracted records into the IProcessPollerReceiver interface
     */
    class ProcessPollerBase {
    public:
        struct IProcessPollerReceiver {
            virtual ~IProcessPollerReceiver() = default;

            /**
             * Called for each /proc/[PID] directory
             */
            virtual void onProcessDirectory(int pid, const lib::FsEntry & path);

            /**
             * Called for each /proc/[PID]/task/[TID] directory
             */
            virtual void onThreadDirectory(int pid, int tid, const lib::FsEntry & path);

            /**
             * Called with the contents of stat, statm and the parsed exe path
             */
            virtual void onThreadDetails(int pid,
                                         int tid,
                                         const ProcPidStatFileRecord & statRecord,
                                         const std::optional<ProcPidStatmFileRecord> & statmRecord,
                                         const std::optional<lib::FsEntry> & exe);
        };

        ProcessPollerBase();
        virtual ~ProcessPollerBase() = default;

    protected:
        void poll(bool wantThreads, bool wantStats, IProcessPollerReceiver & receiver);

    private:
        lib::FsEntry procDir;

        static void processPidDirectory(bool wantThreads,
                                        bool wantStats,
                                        IProcessPollerReceiver & receiver,
                                        const lib::FsEntry & entry);
        static void processTidDirectory(bool wantStats,
                                        IProcessPollerReceiver & receiver,
                                        int pid,
                                        const lib::FsEntry & entry,
                                        const std::optional<lib::FsEntry> & exe);
    };

    /** @return The process exe path (or some estimation of it). Empty if the thread is a kernel thread, otherwise contains 'something' */
    std::optional<lib::FsEntry> getProcessExePath(const lib::FsEntry & entry);
}

#endif /* INCLUDE_LINUX_PROC_PROCESSPOLLERBASE_H */
