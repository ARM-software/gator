/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#include "DriverCounter.h"
#include "Logging.h"
#include "SessionData.h"
#include "lib/Format.h"
#include "lib/FsEntry.h"
#include "non_root/GlobalStatsTracker.h"
#include "non_root/NonRootDriver.h"

#include <cstring>
#include <unistd.h>

namespace non_root
{
    namespace
    {
        class NonRootDriverCounter : public DriverCounter
        {
        public:

            NonRootDriverCounter(DriverCounter * next, NonRootCounter type, const char * name, const char * label, const char * title, const char * description,
                                 const char * display, const char * counterClass, const char * unit, const char * seriesComposition, double multiplier,
                                 bool percpu, bool proc)
                    : DriverCounter(next, name),
                      mType(type),
                      mLabel(label),
                      mTitle(title),
                      mDescription(description),
                      mDisplay(display),
                      mCounterClass(counterClass),
                      mUnit(unit),
                      mSeriesComposition(seriesComposition),
                      mMultiplier(multiplier),
                      mPerCPU(percpu),
                      mProc(proc)
            {
            }

            NonRootDriverCounter(DriverCounter * next, bool system, const std::string & name)
                    : DriverCounter(next, name.c_str()),
                      mType(system ? NonRootCounter::ACTIVITY_SYSTEM : NonRootCounter::ACTIVITY_USER),
                      mLabel(nullptr),
                      mTitle(nullptr),
                      mDescription(nullptr),
                      mDisplay(nullptr),
                      mCounterClass(nullptr),
                      mUnit(nullptr),
                      mSeriesComposition(nullptr),
                      mMultiplier(0),
                      mPerCPU(false),
                      mProc(false)
            {
            }

            NonRootCounter getType() const
            {
                return mType;
            }

            const char * getLabel() const
            {
                return mLabel;
            }

            const char * getTitle() const
            {
                return mTitle;
            }

            const char * getDescription() const
            {
                return mDescription;
            }

            const char * getDisplay() const
            {
                return mDisplay;
            }

            const char * getCounterClass() const
            {
                return mCounterClass;
            }

            const char * getUnit() const
            {
                return mUnit;
            }

            const char * getSeriesComposition() const
            {
                return mSeriesComposition;
            }

            double getMultiplier() const
            {
                return mMultiplier;
            }

            bool isPerCPU() const
            {
                return mPerCPU;
            }

            bool isProc() const
            {
                return mProc;
            }

        private:

            NonRootCounter mType;
            const char * mLabel;
            const char * mTitle;
            const char * mDescription;
            const char * mDisplay;
            const char * mCounterClass;
            const char * mUnit;
            const char * mSeriesComposition;
            double mMultiplier;
            bool mPerCPU;
            bool mProc;

            CLASS_DEFAULT_COPY_MOVE (NonRootDriverCounter)
            ;
        };
    }

    NonRootDriver::NonRootDriver(PmuXML && pmuXml, lib::Span<const GatorCpu> clusters)
            : SimpleDriver("NonRootDriver"),
              pmuXml(std::move(pmuXml)),
              clusters(clusters)
    {
    }

    void NonRootDriver::readEvents(mxml_node_t *)
    {
        const double ticks_mult = 1.0 / sysconf(_SC_CLK_TCK);

        const bool canAccessProcStat = (access("/proc/stat", R_OK) == 0);
        const bool canAccessProcLoadAvg = (access("/proc/loadavg", R_OK) == 0);
        // assume if we can access these for 'self' we can access for other *accessible* PID directories
        const bool canAccessProcSelfStat = (access("/proc/self/stat", R_OK) == 0);
        const bool canAccessProcSelfStatm = (access("/proc/self/statm", R_OK) == 0);

        // non-root counters are fixed so are not listed in events.xml; instead enumerate them here
        if (canAccessProcLoadAvg) {
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_ABS_LOADAVG_1_MINUTE, "nonroot_global_abs_loadavg_1_minute", //
                                                 "Average Over 1 Minute", //
                                                 "Load Average", //
                                                 "Load average figure giving the number of jobs in the run queue (state R) "
                                                 "or waiting for disk I/O (state D) averaged over 1 minute. "
                                                 "See the description of /proc/loadavg in 'man proc.5' for more details. This counter represents field 1.", //
                                                 "average", "absolute", nullptr, "overlay", 1.0 / GlobalStatsTracker::LOADAVG_MULTIPLIER, false, false));
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_ABS_LOADAVG_5_MINUTES, "nonroot_global_abs_loadavg_5_minutes", //
                                                 "Average Over 5 Minutes", //
                                                 "Load Average", //
                                                 "Load average figure giving the number of jobs in the run queue (state R) "
                                                 "or waiting for disk I/O (state D) averaged over 5 minutes. "
                                                 "See the description of /proc/loadavg in 'man proc.5' for more details. This counter represents field 2.",
                                                 "average", "absolute", nullptr, "overlay", 1.0 / GlobalStatsTracker::LOADAVG_MULTIPLIER, false, false));
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_ABS_LOADAVG_15_MINUTES, "nonroot_global_abs_loadavg_15_minutes", //
                                                 "Average Over 15 Minute", //
                                                 "Load Average", //
                                                 "Load average figure giving the number of jobs in the run queue (state R) "
                                                 "or waiting for disk I/O (state D) averaged over 15 minutes. "
                                                 "See the description of /proc/loadavg in 'man proc.5' for more details. This counter represents field 3.", //
                                                 "average", "absolute", nullptr, "overlay", 1.0 / GlobalStatsTracker::LOADAVG_MULTIPLIER, false, false));
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_ABS_NUM_PROCESSES_EXISTING, "nonroot_global_abs_num_processes_existing", //
                                                 "Total Processes", //
                                                 "System", //
                                                 "The number of kernel scheduling entities (processes, threads) that currently exist on the system. "
                                                 "See the description of /proc/loadavg in 'man proc.5' for more details. This counter represents field 4.", //
                                                 "average", "absolute", nullptr, "overlay", 1.0, false, false));
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_ABS_NUM_PROCESSES_RUNNING, "nonroot_global_abs_num_processes_running", //
                                                 "Running Processes", //
                                                 "System", //
                                                 "The number of currently runnable kernel scheduling entities (processes, threads). "
                                                 "See the description of /proc/loadavg in 'man proc.5' for more details. This counter represents field 4.", //
                                                 "average", "absolute", nullptr, "overlay", 1.0, false, false));
        }
        else {
            logg.logSetup("Non-root support\nCannot access /proc/loadavg");
        }

        if (canAccessProcStat) {
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_DELTA_NUM_CONTEXT_SWITCHES, "nonroot_global_delta_num_context_switches", //
                                                 "Context Switches", //
                                                 "Scheduler", //
                                                 "The number of context switches that the system underwent. "
                                                 "See the description of /proc/stat [ctxt] in 'man proc.5' for more details.", //
                                                 "accumulate", "delta", nullptr, "overlay", 1.0, false, false));
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_DELTA_NUM_FORKS, "nonroot_global_delta_num_forks", //
                                                 "Forks", //
                                                 "System", //
                                                 "Number of forks since boot. "
                                                 "See the description of /proc/stat [processes] in 'man proc.5' for more details.", //
                                                 "accumulate", "delta", nullptr, "overlay", 1.0, false, false));
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_DELTA_NUM_IRQ, "nonroot_global_delta_num_irq", //
                                                 "IRQ", //
                                                 "Interrupts", //
                                                 "The total of all interrupts serviced including unnumbered architecture specific interrupts. "
                                                 "See the description of /proc/stat [intr] in 'man proc.5' for more details.", //
                                                 "accumulate", "delta", nullptr, "overlay", 1.0, false, false));
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_DELTA_NUM_SOFTIRQ, "nonroot_global_delta_num_softirq", //
                                                 "Soft IRQ", //
                                                 "Interrupts", //
                                                 "The total of all softirqs serviced. "
                                                 "See the description of /proc/stat [softirq] in 'man proc.5' for more details.", //
                                                 "accumulate", "delta", nullptr, "overlay", 1.0, false, false));
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_DELTA_TIME_CPU_GUEST_NICE, "nonroot_global_delta_time_cpu_guest_nice", //
                                                 "Guest Nice", //
                                                 "CPU Times", //
                                                 "The amount of time, measured in units of USER_HZ, "
                                                 "that the system spent running a niced guest (virtual CPU for guest operating systems under "
                                                 "the control of the Linux kernel). "
                                                 "See the description of /proc/stat [cpu.guest_nice] in 'man proc.5' for more details.", //
                                                 "accumulate", "delta", "s", "stacked", ticks_mult, true, false));
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_DELTA_TIME_CPU_GUEST, "nonroot_global_delta_time_cpu_guest", //
                                                 "Guest", //
                                                 "CPU Times", //
                                                 "The amount of time, measured in units of USER_HZ, "
                                                 "that the system spent running a virtual CPU for guest operating systems under "
                                                 "the control of the Linux kernel. "
                                                 "See the description of /proc/stat [cpu.guest] in 'man proc.5' for more details.", //
                                                 "accumulate", "delta", "s", "stacked", ticks_mult, true, false));
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_DELTA_TIME_CPU_IDLE, "nonroot_global_delta_time_cpu_idle", //
                                                 "Idle", //
                                                 "CPU Times", //
                                                 "The amount of time, measured in units of USER_HZ, "
                                                 "that the system spent in the idle task. "
                                                 "See the description of /proc/stat [cpu.idle] in 'man proc.5' for more details.", //
                                                 "accumulate", "delta", "s", "stacked", ticks_mult, true, false));
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_DELTA_TIME_CPU_IOWAIT, "nonroot_global_delta_time_cpu_iowait", //
                                                 "IO wait", //
                                                 "CPU Times", //
                                                 "The amount of time, measured in units of USER_HZ, "
                                                 "that the system spent waiting for I/O to complete. "
                                                 "See the description of /proc/stat [cpu.iowait] in 'man proc.5' for more details.", //
                                                 "accumulate", "delta", "s", "stacked", ticks_mult, true, false));
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_DELTA_TIME_CPU_IRQ, "nonroot_global_delta_time_cpu_irq", //
                                                 "IRQ", //
                                                 "CPU Times", //
                                                 "The amount of time, measured in units of USER_HZ, "
                                                 "that the system spent servicing interrupts. "
                                                 "See the description of /proc/stat [cpu.irq] in 'man proc.5' for more details.", //
                                                 "accumulate", "delta", "s", "stacked", ticks_mult, true, false));
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_DELTA_TIME_CPU_NICE, "nonroot_global_delta_time_cpu_nice", //
                                                 "Nice", //
                                                 "CPU Times", //
                                                 "The amount of time, measured in units of USER_HZ, "
                                                 "that the system spent servicing in user mode with low priority (nice). "
                                                 "See the description of /proc/stat [cpu.nice] in 'man proc.5' for more details.", //
                                                 "accumulate", "delta", "s", "stacked", ticks_mult, true, false));
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_DELTA_TIME_CPU_SOFTIRQ, "nonroot_global_delta_time_cpu_softirq", //
                                                 "Soft IRQ", //
                                                 "CPU Times", //
                                                 "The amount of time, measured in units of USER_HZ, "
                                                 "that the system spent servicing softirqs. "
                                                 "See the description of /proc/stat [cpu.softirq] in 'man proc.5' for more details.", //
                                                 "accumulate", "delta", "s", "stacked", ticks_mult, true, false));
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_DELTA_TIME_CPU_STEAL, "nonroot_global_delta_time_cpu_steal", //
                                                 "Steal", //
                                                 "CPU Times", //
                                                 "The amount of time, measured in units of USER_HZ, "
                                                 "that the system spent in other operating systems when running in a virtualized environment. "
                                                 "See the description of /proc/stat [cpu.steal] in 'man proc.5' for more details.", //
                                                 "accumulate", "delta", "s", "stacked", ticks_mult, true, false));
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_DELTA_TIME_CPU_SYSTEM, "nonroot_global_delta_time_cpu_system", //
                                                 "System", //
                                                 "CPU Times", //
                                                 "The amount of time, measured in units of USER_HZ, that the system spent in system mode. "
                                                 "See the description of /proc/stat [cpu.system] in 'man proc.5' for more details.", //
                                                 "accumulate", "delta", "s", "stacked", ticks_mult, true, false));
            setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::GLOBAL_DELTA_TIME_CPU_USER, "nonroot_global_delta_time_cpu_user", //
                                                 "User", //
                                                 "CPU Times", //
                                                 "The amount of time, measured in units of USER_HZ, that the system spent in user mode. "
                                                 "See the description of /proc/stat [cpu.user] in 'man proc.5' for more details.", //
                                                 "accumulate", "delta", "s", "stacked", ticks_mult, true, false));
        }
        else {
            logg.logSetup("Non-root support\nCannot access /proc/stat");
        }

        static const int ANDROID_N_API_LEVEL = 24;

        if (gSessionData.mAndroidApiLevel < ANDROID_N_API_LEVEL) {
            // Android 7 severly restricts access to /proc filesystem on so we are unable to access other processes proc files.
            // disable all per-process counters including CPU activity counters.
            if (canAccessProcSelfStatm) {
                setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::PROCESS_ABS_DATA_SIZE, "nonroot_process_abs_data_size", //
                                                     "Data Size", //
                                                     "Process (Memory)", //
                                                     "Total size of data + stack in bytes. "
                                                     "See the description of /proc/[PID]/statm [data] in 'man proc.5' for more details.", //
                                                     "maximum", "absolute", "B", "overlay", 1.0, false, true));
                setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::PROCESS_ABS_SHARED_SIZE, "nonroot_process_abs_shared_size", //
                                                     "Shared Size", //
                                                     "Process (Memory)", //
                                                     "Total size of resident shared pages (i.e., backed by a file). "
                                                     "See the description of /proc/[PID]/statm [shared] in 'man proc.5' for more details.", //
                                                     "maximum", "absolute", "B", "overlay", 1.0, false, true));
                setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::PROCESS_ABS_TEXT_SIZE, "nonroot_process_abs_text_size", //
                                                     "Text Size", //
                                                     "Process (Memory)", //
                                                     "Total size of text (code) in bytes. "
                                                     "See the description of /proc/[PID]/statm [text] in 'man proc.5' for more details.", //
                                                     "maximum", "absolute", "B", "overlay", 1.0, false, true));
            }
            else {
                logg.logSetup("Non-root support\nCannot access /proc/self/statm");
            }

            if (canAccessProcSelfStat) {
                setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::PROCESS_ABS_NUM_THREADS, "nonroot_process_abs_num_threads", //
                                                     "Num Threads", //
                                                     "Process (Threads)", //
                                                     "Number of threads in this process. "
                                                     "See the description of /proc/[PID]/stat [num_threads] in 'man proc.5' for more details.", //
                                                     "maximum", "absolute", nullptr, "overlay", 1.0, false, true));
                setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::PROCESS_ABS_RES_LIMIT, "nonroot_process_abs_res_limit", //
                                                     "Res Limit", //
                                                     "Process (Memory)", //
                                                     "Current soft limit in bytes on the rss of the process. "
                                                     "See the description of /proc/[PID]/stat [rsslim] in 'man proc.5' for more details.", //
                                                     "maximum", "absolute", "B", "overlay", 1.0, false, true));
                setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::PROCESS_ABS_RES_SIZE, "nonroot_process_abs_res_size", //
                                                     "Res Size", //
                                                     "Process (Memory)", //
                                                     "Resident Set Size: number of pages the process has in real memory. "
                                                     "This is just the pages which count toward text, data, or stack space. "
                                                     "This does not include pages which have not been demand-loaded in, or which are swapped out. "
                                                     "See the description of /proc/[PID]/stat [rss] in 'man proc.5' for more details.", //
                                                     "maximum", "absolute", "B", "overlay", 1.0, false, true));
                setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::PROCESS_ABS_VM_SIZE, "nonroot_process_abs_vm_size", //
                                                     "VM Size", //
                                                     "Process (Memory)", //
                                                     "Virtual memory size in bytes. "
                                                     "See the description of /proc/[PID]/stat [vsize] in 'man proc.5' for more details.", //
                                                     "maximum", "absolute", "B", "overlay", 1.0, false, true));
                setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::PROCESS_DELTA_MAJOR_FAULTS, "nonroot_process_delta_major_faults", //
                                                     "Major Faults", //
                                                     "Process (Faults)", //
                                                     "The number of major faults the process has made which have required loading a memory page from disk. "
                                                     "See the description of /proc/[PID]/stat [majflt] in 'man proc.5' for more details.", //
                                                     "accumulate", "delta", nullptr, "overlay", 1.0, false, true));
                setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::PROCESS_DELTA_MINOR_FAULTS, "nonroot_process_delta_minor_faults", //
                                                     "Minor Faults", //
                                                     "Process (Faults)", //
                                                     "The number of minor faults the process has made which have not required loading a memory page from disk. "
                                                     "See the description of /proc/[PID]/stat [minflt] in 'man proc.5' for more details.", //
                                                     "accumulate", "delta", nullptr, "overlay", 1.0, false, true));
                setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::PROCESS_DELTA_UTIME, "nonroot_process_delta_utime", //
                                                     "Userspace", //
                                                     "Process (CPU Times)", //
                                                     "Amount of time that this process has been scheduled in user mode (including guest time)." //
                                                     "See the description of /proc/[PID]/stat [utime] in 'man proc.5' for more details.", //
                                                     "accumulate", "delta", "s", "stacked", ticks_mult, false, true));
                setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::PROCESS_DELTA_STIME, "nonroot_process_delta_stime", //
                                                     "Kernel", //
                                                     "Process (CPU Times)", //
                                                     "Amount of time that this process has been scheduled in kernel mode. "
                                                     "See the description of /proc/[PID]/stat [stime] in 'man proc.5' for more details.", //
                                                     "accumulate", "delta", "s", "stacked", ticks_mult, false, true));
                setCounters(new NonRootDriverCounter(getCounters(), NonRootCounter::PROCESS_DELTA_GUEST_TIME, "nonroot_process_delta_guest_time", //
                                                     "Guest", //
                                                     "Process (CPU Times)", //
                                                     "Guest time of the process (time spent running a virtual CPU for a guest operating system). " //
                                                     "See the description of /proc/[PID]/stat [guest_time] in 'man proc.5' for more details.", //
                                                     "accumulate", "delta", "s", "stacked", ticks_mult, false, true));

                // CPU activity charts
                for (const GatorCpu & cluster : clusters) {
                    const std::string sysName = (lib::Format() << cluster.getPmncName() << "_system");
                    const std::string userName = (lib::Format() << cluster.getPmncName() << "_user");

                    setCounters(new NonRootDriverCounter(getCounters(), true, sysName));
                    setCounters(new NonRootDriverCounter(getCounters(), false, userName));
                }
            }
            else {
                logg.logSetup("Non-root support\nCannot access /proc/self/stat");
            }
        }
        else {
            logg.logSetup("Non-root limited on Android 7+\nDisabled per-process non-root counters on Android 7+ due to access restrictions on /proc (Android API level detected as %d)", gSessionData.mAndroidApiLevel);
        }
    }

    void NonRootDriver::writeEvents(mxml_node_t * root) const
    {
        root = mxmlNewElement(root, "category");
        mxmlElementSetAttr(root, "name", "Non-Root");

        for (NonRootDriverCounter *counter = static_cast<NonRootDriverCounter *>(getCounters()); counter != nullptr;
                counter = static_cast<NonRootDriverCounter *>(counter->getNext())) {

            if ((counter->getType() != NonRootCounter::ACTIVITY_SYSTEM) && (counter->getType() != NonRootCounter::ACTIVITY_USER)) {
                mxml_node_t * node = mxmlNewElement(root, "event");
                mxmlElementSetAttr(node, "counter", counter->getName());
                mxmlElementSetAttr(node, "title", counter->getTitle());
                mxmlElementSetAttr(node, "name", counter->getLabel());
                mxmlElementSetAttr(node, "display", counter->getDisplay());
                mxmlElementSetAttr(node, "class", counter->getCounterClass());
                if (counter->getUnit() != nullptr) {
                    mxmlElementSetAttr(node, "units", counter->getUnit());
                }
                if (counter->getMultiplier() != 1.0) {
                    mxmlElementSetAttrf(node, "multiplier", "%f", counter->getMultiplier());
                }
                if (strcmp(counter->getDisplay(), "average") == 0 || strcmp(counter->getDisplay(), "maximum") == 0) {
                    mxmlElementSetAttr(node, "average_selection", "yes");
                }
                if (counter->isPerCPU()) {
                    mxmlElementSetAttr(node, "per_cpu", "yes");
                }
                mxmlElementSetAttr(node, "proc", counter->isProc() ? "yes" : "no");
                mxmlElementSetAttr(node, "series_composition", counter->getSeriesComposition());
                mxmlElementSetAttr(node, "rendering_type", "line");
                mxmlElementSetAttr(node, "description", counter->getDescription());
            }
        }
    }

    std::map<NonRootCounter, int> NonRootDriver::getEnabledCounters() const
    {
        std::map<NonRootCounter, int> result;

        for (NonRootDriverCounter *counter = static_cast<NonRootDriverCounter *>(getCounters()); counter != nullptr;
                counter = static_cast<NonRootDriverCounter *>(counter->getNext())) {
            if (counter->isEnabled()) {
                result[counter->getType()] = counter->getKey();
            }
        }

        return result;
    }
}
