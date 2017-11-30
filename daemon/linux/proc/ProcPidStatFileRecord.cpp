/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#include "linux/proc/ProcPidStatFileRecord.h"

#include <cstring>
#include <cstdio>

namespace lnx
{
    namespace
    {
        static const char PROC_STAT_SCANF_FORMAT_BEFORE_COMM[] = "%d (";
        static const char PROC_STAT_SCANF_FORMAT_AFTER_COMM[] = ") %c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu %ld %ld %ld %ld %ld %ld %llu %lu %ld %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %d %d %u %u %llu %lu %ld";
        static constexpr const int PROC_STAT_SCANF_BEFORE_COMM_FIELD_COUNT = 1;
        static constexpr const int PROC_STAT_SCANF_AFTER_COMM_FIELD_COUNT = 42;
    }

    bool ProcPidStatFileRecord::parseStatFile(ProcPidStatFileRecord & result, const char * stat_contents)
    {
        if (stat_contents == nullptr) {
            return false;
        }

        // separate out comm, which is surrounded by parenthesis. dont us sscanf
        const char * const comm_start = std::strchr(stat_contents, '(');
        const char * const comm_end = std::strrchr(stat_contents, ')');

        if ((comm_start == nullptr) || (comm_end == nullptr)) {
            return false;
        }

        // parse the items before comm (just pid)
        const int nscanned_before_comm = sscanf(stat_contents, PROC_STAT_SCANF_FORMAT_BEFORE_COMM, &result.pid);
        if (nscanned_before_comm != PROC_STAT_SCANF_BEFORE_COMM_FIELD_COUNT) {
            return false;
        }

        // parse the items after comm
        const int nscanned_after_comm = sscanf(comm_end, PROC_STAT_SCANF_FORMAT_AFTER_COMM, &result.state, &result.ppid,
                                               &result.pgid, &result.session, &result.tty_nr, &result.tpgid,
                                               &result.flags, &result.minflt, &result.cminflt, &result.majflt,
                                               &result.cmajflt, &result.utime, &result.stime, &result.cutime,
                                               &result.cstime, &result.priority, &result.nice, &result.num_threads,
                                               &result.itrealvalue, &result.starttime, &result.vsize, &result.rss,
                                               &result.rsslim, &result.startcode, &result.endcode, &result.startstack,
                                               &result.kstkesp, &result.kstkeip, &result.signal, &result.blocked,
                                               &result.sigignore, &result.sigcatch, &result.wchan, &result.nswap,
                                               &result.cnswap, &result.exit_signal, &result.processor,
                                               &result.rt_priority, &result.policy, &result.delayacct_blkio_ticks,
                                               &result.guest_time, &result.cguest_time);

        if (nscanned_after_comm != PROC_STAT_SCANF_AFTER_COMM_FIELD_COUNT) {
            return false;
        }

        // copy comm value
        result.comm.assign(comm_start + 1, comm_end);

        return true;
    }

    ProcPidStatFileRecord::ProcPidStatFileRecord()
            : state(0),
              pid(0),
              ppid(0),
              pgid(0),
              session(0),
              tty_nr(0),
              tpgid(0),
              exit_signal(0),
              processor(0),
              flags(0),
              rt_priority(0),
              policy(0),
              cutime(0),
              cstime(0),
              priority(0),
              nice(0),
              num_threads(0),
              itrealvalue(0),
              rss(0),
              cguest_time(0),
              minflt(0),
              cminflt(0),
              majflt(0),
              cmajflt(0),
              utime(0),
              stime(0),
              vsize(0),
              rsslim(0),
              startcode(0),
              endcode(0),
              startstack(0),
              kstkesp(0),
              kstkeip(0),
              signal(0),
              blocked(0),
              sigignore(0),
              sigcatch(0),
              wchan(0),
              nswap(0),
              cnswap(0),
              guest_time(0),
              starttime(0),
              delayacct_blkio_ticks(0),
              comm()
    {
    }

    ProcPidStatFileRecord::ProcPidStatFileRecord(int pid_, const char * comm_, char state_, int ppid_, int pgid_, int session_, int tty_nr_,
                                   int tpgid_, unsigned flags_, unsigned long minflt_, unsigned long cminflt_,
                                   unsigned long majflt_, unsigned long cmajflt_, unsigned long utime_,
                                   unsigned long stime_, long cutime_, long cstime_, long priority_, long nice_,
                                   long num_threads_, long itrealvalue_, unsigned long long starttime_,
                                   unsigned long vsize_, long rss_, unsigned long rsslim_, unsigned long startcode_,
                                   unsigned long endcode_, unsigned long startstack_, unsigned long kstkesp_,
                                   unsigned long kstkeip_, unsigned long signal_, unsigned long blocked_,
                                   unsigned long sigignore_, unsigned long sigcatch_, unsigned long wchan_,
                                   unsigned long nswap_, unsigned long cnswap_, int exit_signal_, int processor_,
                                   unsigned rt_priority_, unsigned policy_, unsigned long long delayacct_blkio_ticks_,
                                   unsigned long guest_time_, long cguest_time_)
            : state(state_),
              pid(pid_),
              ppid(ppid_),
              pgid(pgid_),
              session(session_),
              tty_nr(tty_nr_),
              tpgid(tpgid_),
              exit_signal(exit_signal_),
              processor(processor_),
              flags(flags_),
              rt_priority(rt_priority_),
              policy(policy_),
              cutime(cutime_),
              cstime(cstime_),
              priority(priority_),
              nice(nice_),
              num_threads(num_threads_),
              itrealvalue(itrealvalue_),
              rss(rss_),
              cguest_time(cguest_time_),
              minflt(minflt_),
              cminflt(cminflt_),
              majflt(majflt_),
              cmajflt(cmajflt_),
              utime(utime_),
              stime(stime_),
              vsize(vsize_),
              rsslim(rsslim_),
              startcode(startcode_),
              endcode(endcode_),
              startstack(startstack_),
              kstkesp(kstkesp_),
              kstkeip(kstkeip_),
              signal(signal_),
              blocked(blocked_),
              sigignore(sigignore_),
              sigcatch(sigcatch_),
              wchan(wchan_),
              nswap(nswap_),
              cnswap(cnswap_),
              guest_time(guest_time_),
              starttime(starttime_),
              delayacct_blkio_ticks(delayacct_blkio_ticks_),
              comm(comm_)
    {
    }
}
