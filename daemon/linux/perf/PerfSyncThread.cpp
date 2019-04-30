/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfSyncThread.h"
#include "lib/GenericTimer.h"

#include "Logging.h"
#include "lib/Assert.h"

#include <cstring>
#include <cerrno>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <signal.h>
#include <unistd.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#include <sched.h>
#undef _GNU_SOURCE
#else
#include <sched.h>
#endif

namespace
{
    inline std::uint64_t get_cntfreq_el0(bool readTimer)
    {
        if (readTimer) {
            return lib::get_cntfreq_el0();
        }

        return 0;
    }

    inline std::uint64_t get_cntvct_el0(bool readTimer)
    {
        if (readTimer) {
            return lib::get_cntvct_el0();
        }
        return 0;
    }
}

extern std::uint64_t getTime();

#define NS_PER_S        1000000000ULL
#define NS_TO_US        1000ULL
#define NS_TO_SLEEP     (NS_PER_S / 2)

PerfSyncThread::PerfSyncThread(unsigned cpu, bool enableSyncThreadMode, bool readTimer, std::uint64_t monotonicRawBase, ConsumerFunction consumerFunction)
        : thread(),
          consumerFunction(consumerFunction),
          monotonicRawBase(monotonicRawBase),
          cpu(cpu),
          terminateFlag(false),
          readTimer(readTimer),
          enableSyncThreadMode(enableSyncThreadMode)
{
    runtime_assert(enableSyncThreadMode || readTimer, "At least one of enableSyncThreadMode or readTimer are required");

    thread = std::thread(launch, this);
}

PerfSyncThread::~PerfSyncThread()
{
    if (!terminateFlag.load(std::memory_order_relaxed)) {
        terminate();
    }
}

void PerfSyncThread::terminate()
{
    terminateFlag.store(true, std::memory_order_release);
    thread.join();
}

void PerfSyncThread::launch(PerfSyncThread * _this) noexcept
{
    _this->run();
}

void PerfSyncThread::rename(std::uint64_t currentTime)
{
    // we need a way to provoke a record to appear in the perf ring buffer that we can correlated back to an action here
    // rename thread which will generate a PERF_RECORD_COMM - encode the monotonic delta in uSeconds into the name
    // this allows us to work out ~ what the start time was relative to the local-clock event
    if (enableSyncThreadMode && (cpu == 0)) {
        char buffer[16];
        const std::uint64_t uSeconds = (currentTime - monotonicRawBase) / NS_TO_US;
        if (uSeconds <= 9999999999ull) {
            snprintf(buffer, sizeof(buffer), "gds-%010u-", static_cast<unsigned>(uSeconds));
            prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&buffer[0]), 0, 0, 0);
        }
        else {
            prctl(PR_SET_NAME, reinterpret_cast<unsigned long>("gator-sync-0"), 0, 0, 0);
        }
    }
}

void PerfSyncThread::run() noexcept
{
    // get pid and tid
    const pid_t pid = getpid();
    const pid_t tid = syscall(__NR_gettid);

    // affine the thread to a single CPU
    {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu, &cpuset);

        if (sched_setaffinity(tid, sizeof(cpu_set_t), &cpuset) != 0) {
            logg.logError("Error calling sched_setaffinity on %u: %d (%s)", cpu, errno, strerror(errno));
            handleException();
        }
    }

    // change thread priority
    {
        struct sched_param param;
        param.sched_priority = sched_get_priority_max(SCHED_FIFO);
        if (sched_setscheduler(tid, SCHED_FIFO | SCHED_RESET_ON_FORK, &param) != 0) {
            logg.logMessage("Unable to schedule sync thread as FIFO, trying OTHER: %d (%s)", errno, strerror(errno));
            param.sched_priority = sched_get_priority_max(SCHED_OTHER);
            if (sched_setscheduler(tid, SCHED_OTHER | SCHED_RESET_ON_FORK, &param) != 0) {
                logg.logMessage("sched_setscheduler failed for %u: %d (%s)", cpu, errno, strerror(errno));
            }
        }
    }

    // Mask all signals so that this thread will not be woken up
    {
        sigset_t set;
        if (sigfillset(&set) != 0) {
            logg.logError("sigfillset failed: %d (%s)", errno, strerror(errno));
            handleException();
        }
        if (sigprocmask(SIG_SETMASK, &set, nullptr) != 0) {
            logg.logError("sigprocmask failed %d (%s)", errno, strerror(errno));
            handleException();
        }
    }

    // yield the thread so that we are on the correct cpu and to reduce the likelihood of yielding before sync starts
    sched_yield();

    // rename thread
    {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "gator-sync-%u", cpu);
        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&buffer[0]), 0, 0, 0);
    }

    // main loop (always executes at least once to ensure we always capture at least one sync point
    do {
        // get current timestamp
        const std::uint64_t syncTime = getTime();

        // read CNTFREQ_EL0
        const std::uint64_t frequency = get_cntfreq_el0(readTimer);

        // get architectural timer for SPE sync
        const std::uint64_t vcount = get_cntvct_el0(readTimer);

        // send the updated name with the monotonic delta
        rename(syncTime);

        // send the data to the consumer
        consumerFunction(cpu, pid, tid, frequency, syncTime, vcount);

        // sleep for short period
        struct timespec ts;
        ts.tv_sec = NS_TO_SLEEP / NS_PER_S;
        ts.tv_nsec = NS_TO_SLEEP % NS_PER_S;
        if (nanosleep(&ts, nullptr) != 0) {
            logg.logError("nanosleep failed for %u: %d (%s)", cpu, errno, strerror(errno));
            handleException();
        }
    } while (!terminateFlag.load(std::memory_order_acquire));
}
