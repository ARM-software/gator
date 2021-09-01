/* Copyright (C) 2017-2021 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_PROC_PROCPIDSTATFILERECORD_H
#define INCLUDE_LINUX_PROC_PROCPIDSTATFILERECORD_H

#include <string>

namespace lnx {
    /**
     * The parsed contents of /proc/[pid]/stat as per `man proc.5`.
     *
     * NB: Tracks only the first 44 fields, as currently field 45 and above (which are only defined in kernel 3.3 above with PT set) are not required
     */
    class ProcPidStatFileRecord {
    public:
        /**
         * Parse the contents of stat file, modifying the fields in 'result' if successful.
         *
         * @param result The object to store the extracted fields in
         * @param stat_contents The text contents of the stat file
         * @return True if the contents were successfully parsed, false otherwise
         */
        static bool parseStatFile(ProcPidStatFileRecord & result, const char * stat_contents);

        /**
         * Create an empty record with all fields null/zero/empty
         */
        ProcPidStatFileRecord() = default;

        /**
         * Create and populate a record
         */
        ProcPidStatFileRecord(int pid,
                              const char * comm,
                              char state,
                              int ppid,
                              int pgid,
                              int session,
                              int tty_nr,
                              int tpgid,
                              unsigned flags,
                              unsigned long minflt,
                              unsigned long cminflt,
                              unsigned long majflt,
                              unsigned long cmajflt,
                              unsigned long utime,
                              unsigned long stime,
                              long cutime,
                              long cstime,
                              long priority,
                              long nice,
                              long num_threads,
                              long itrealvalue,
                              unsigned long long starttime,
                              unsigned long vsize,
                              long rss,
                              unsigned long rsslim,
                              unsigned long startcode,
                              unsigned long endcode,
                              unsigned long startstack,
                              unsigned long kstkesp,
                              unsigned long kstkeip,
                              unsigned long signal,
                              unsigned long blocked,
                              unsigned long sigignore,
                              unsigned long sigcatch,
                              unsigned long wchan,
                              unsigned long nswap,
                              unsigned long cnswap,
                              int exit_signal,
                              int processor,
                              unsigned rt_priority,
                              unsigned policy,
                              unsigned long long delayacct_blkio_ticks,
                              unsigned long guest_time,
                              long cguest_time);

        unsigned long getBlocked() const { return blocked; }

        long getCguestTime() const { return cguest_time; }

        unsigned long getCmajflt() const { return cmajflt; }

        unsigned long getCminflt() const { return cminflt; }

        unsigned long getCnswap() const { return cnswap; }

        const std::string & getComm() const { return comm; }

        long getCstime() const { return cstime; }

        long getCutime() const { return cutime; }

        unsigned long long getDelayacctBlkioTicks() const { return delayacct_blkio_ticks; }

        unsigned long getEndcode() const { return endcode; }

        int getExitSignal() const { return exit_signal; }

        unsigned getFlags() const { return flags; }

        unsigned long getGuestTime() const { return guest_time; }

        long getItrealvalue() const { return itrealvalue; }

        unsigned long getKstkeip() const { return kstkeip; }

        unsigned long getKstkesp() const { return kstkesp; }

        unsigned long getMajflt() const { return majflt; }

        unsigned long getMinflt() const { return minflt; }

        long getNice() const { return nice; }

        unsigned long getNswap() const { return nswap; }

        long getNumThreads() const { return num_threads; }

        int getPgid() const { return pgid; }

        int getPid() const { return pid; }

        unsigned getPolicy() const { return policy; }

        int getPpid() const { return ppid; }

        long getPriority() const { return priority; }

        int getProcessor() const { return processor; }

        long getRss() const { return rss; }

        unsigned long getRsslim() const { return rsslim; }

        unsigned getRtPriority() const { return rt_priority; }

        int getSession() const { return session; }

        unsigned long getSigcatch() const { return sigcatch; }

        unsigned long getSigignore() const { return sigignore; }

        unsigned long getSignal() const { return signal; }

        unsigned long getStartcode() const { return startcode; }

        unsigned long getStartstack() const { return startstack; }

        unsigned long long getStarttime() const { return starttime; }

        char getState() const { return state; }

        unsigned long getStime() const { return stime; }

        int getTpgid() const { return tpgid; }

        int getTtyNr() const { return tty_nr; }

        unsigned long getUtime() const { return utime; }

        unsigned long getVsize() const { return vsize; }

        unsigned long getWchan() const { return wchan; }

    private:
        char state {0};
        int pid {0};
        int ppid {0};
        int pgid {0};
        int session {0};
        int tty_nr {0};
        int tpgid {0};
        int exit_signal {0};
        int processor {0};
        unsigned flags {0};
        unsigned rt_priority {0};
        unsigned policy {0};
        long cutime {0};
        long cstime {0};
        long priority {0};
        long nice {0};
        long num_threads {0};
        long itrealvalue {0};
        long rss {0};
        long cguest_time {0};
        unsigned long minflt {0};
        unsigned long cminflt {0};
        unsigned long majflt {0};
        unsigned long cmajflt {0};
        unsigned long utime {0};
        unsigned long stime {0};
        unsigned long vsize {0};
        unsigned long rsslim {0};
        unsigned long startcode {0};
        unsigned long endcode {0};
        unsigned long startstack {0};
        unsigned long kstkesp {0};
        unsigned long kstkeip {0};
        unsigned long signal {0};
        unsigned long blocked {0};
        unsigned long sigignore {0};
        unsigned long sigcatch {0};
        unsigned long wchan {0};
        unsigned long nswap {0};
        unsigned long cnswap {0};
        unsigned long guest_time {0};
        unsigned long long starttime {0};
        unsigned long long delayacct_blkio_ticks {0};
        std::string comm {};
    };
}

#endif /* INCLUDE_LINUX_PROC_PROCPIDSTATFILERECORD_H */
