/* Copyright (C) 2017-2021 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_PROC_PROCSTATFILERECORD_H
#define INCLUDE_LINUX_PROC_PROCSTATFILERECORD_H

#include <optional>
#include <vector>

namespace lnx {
    /**
     * The parsed contents of /proc/stat as per `man proc.5`
     */
    class ProcStatFileRecord {
    public:
        static constexpr const unsigned long GLOBAL_CPU_TIME_ID = ~0UL;

        /**
         * `cpu` record, time is converted to nanoseconds from USER_HZ numbers
         */
        struct CpuTime {
            unsigned long cpu_id;
            unsigned long long user_ticks;
            unsigned long long nice_ticks;
            unsigned long long system_ticks;
            unsigned long long idle_ticks;
            unsigned long long iowait_ticks;
            unsigned long long irq_ticks;
            unsigned long long softirq_ticks;
            unsigned long long steal_ticks;
            unsigned long long guest_ticks;
            unsigned long long guest_nice_ticks;

            CpuTime() : CpuTime(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) {}

            CpuTime(unsigned long cpu_id_, const unsigned long long (&times_)[10])
                : CpuTime(cpu_id_,
                          times_[0],
                          times_[1],
                          times_[2],
                          times_[3],
                          times_[4],
                          times_[5],
                          times_[6],
                          times_[7],
                          times_[8],
                          times_[9])
            {
            }

            CpuTime(unsigned long cpu_id_,
                    unsigned long long user_ticks_,
                    unsigned long long nice_ticks_,
                    unsigned long long system_ticks_,
                    unsigned long long idle_ticks_,
                    unsigned long long iowait_ticks_,
                    unsigned long long irq_ticks_,
                    unsigned long long softirq_ticks_,
                    unsigned long long steal_ticks_,
                    unsigned long long guest_ticks_,
                    unsigned long long guest_nice_ticks_)
                : cpu_id(cpu_id_),
                  user_ticks(user_ticks_),
                  nice_ticks(nice_ticks_),
                  system_ticks(system_ticks_),
                  idle_ticks(idle_ticks_),
                  iowait_ticks(iowait_ticks_),
                  irq_ticks(irq_ticks_),
                  softirq_ticks(softirq_ticks_),
                  steal_ticks(steal_ticks_),
                  guest_ticks(guest_ticks_),
                  guest_nice_ticks(guest_nice_ticks_)
            {
            }
        };

        /**
         * `page`/`swap` records
         */
        struct PagingCounts {
            unsigned long in;
            unsigned long out;

            PagingCounts() : PagingCounts(0, 0) {}

            PagingCounts(unsigned long in_, unsigned long out_) : in(in_), out(out_) {}
        };

        /**
         * Create an empty record with all fields null/zero/empty
         */
        ProcStatFileRecord() = default;

        /**
         * Construct a record populated with the specified values
         */
        ProcStatFileRecord(std::vector<CpuTime> && cpus,
                           std::optional<PagingCounts> && page,
                           std::optional<PagingCounts> && swap,
                           std::optional<unsigned long> && intr,
                           std::optional<unsigned long> && soft_irq,
                           std::optional<unsigned long> && ctxt,
                           std::optional<unsigned long> && btime,
                           std::optional<unsigned long> && processes,
                           std::optional<unsigned long> && procs_running,
                           std::optional<unsigned long> && procs_blocked);

        /**
         * Parse the contents of /proc/stat, fill in optional fields with any values detected
         */
        ProcStatFileRecord(const char * stat_contents);

        const std::optional<unsigned long> & getBtime() const { return btime; }

        const std::vector<CpuTime> & getCpus() const { return cpus; }

        const std::optional<unsigned long> & getCtxt() const { return ctxt; }

        const std::optional<unsigned long> & getIntr() const { return intr; }

        const std::optional<PagingCounts> & getPage() const { return page; }

        const std::optional<unsigned long> & getProcesses() const { return processes; }

        const std::optional<unsigned long> & getProcsBlocked() const { return procs_blocked; }

        const std::optional<unsigned long> & getProcsRunning() const { return procs_running; }

        const std::optional<unsigned long> & getSoftIrq() const { return soft_irq; }

        const std::optional<PagingCounts> & getSwap() const { return swap; }

    private:
        std::vector<CpuTime> cpus {};
        std::optional<PagingCounts> page {};
        std::optional<PagingCounts> swap {};
        std::optional<unsigned long> intr {};
        std::optional<unsigned long> soft_irq {};
        std::optional<unsigned long> ctxt {};
        std::optional<unsigned long> btime {};
        std::optional<unsigned long> processes {};
        std::optional<unsigned long> procs_running {};
        std::optional<unsigned long> procs_blocked {};
    };
}

#endif /* INCLUDE_LINUX_PROC_PROCSTATFILERECORD_H */
