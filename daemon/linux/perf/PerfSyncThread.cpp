/* Copyright (C) 2018-2024 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfSyncThread.h"

#include "Logging.h"
#include "lib/Assert.h"
#include "lib/Error.h"
#include "lib/GenericTimer.h"
#include "lib/String.h"
#include "lib/Syscall.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>
#include <utility>

#include <sys/prctl.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#include <sched.h>
#undef _GNU_SOURCE
#else
#include <sched.h>
#endif

namespace {
    /// Try to set max priority with either FIFO or OTHER scheduling
    bool set_thread_scheduling(pid_t tid)
    {
        struct sched_param param;

        // Try FIFO scheduling
        param.sched_priority = sched_get_priority_max(SCHED_FIFO);
        if (param.sched_priority == -1) {
            LOG_DEBUG("Unable to sched_get_priority_max(SCHED_FIFO): %d (%s)", errno, lib::strerror());
        }
        else {
            if (sched_setscheduler(tid, SCHED_FIFO | SCHED_RESET_ON_FORK, &param) != 0) {
                LOG_DEBUG("Unable to schedule sync thread as FIFO, trying OTHER: %d (%s)", errno, lib::strerror());
            }
            else {
                return true;
            }
        }

        // Try OTHER (round-robin) scheduling
        param.sched_priority = sched_get_priority_max(SCHED_OTHER);
        if (param.sched_priority == -1) {
            LOG_WARNING("Unable to sched_get_priority_max(SCHED_OTHER): %d (%s)", errno, lib::strerror());
            return false;
        }

        if (sched_setscheduler(tid, SCHED_OTHER | SCHED_RESET_ON_FORK, &param) != 0) {
            // Note: This is not implemented in musl, so failure is expected and not loudly reported [SDDAP-13577]
            //NOLINTNEXTLINE(concurrency-mt-unsafe)
            LOG_DEBUG("sched_setscheduler failed: %d (%s)", errno, lib::strerror());
            return false;
        }

        return true;
    }
}

extern std::uint64_t getTime();

#define NS_PER_S 1000000000ULL
#define NS_TO_US 1000ULL
#define NS_TO_SLEEP (NS_PER_S / 2)

PerfSyncThread::PerfSyncThread(bool enableSyncThreadMode, bool readTimer, ConsumerFunction consumerFunction)
    : consumerFunction(std::move(consumerFunction)), readTimer(readTimer), enableSyncThreadMode(enableSyncThreadMode)
{
    runtime_assert(enableSyncThreadMode || readTimer, "At least one of enableSyncThreadMode or readTimer are required");
}

PerfSyncThread::~PerfSyncThread()
{
    if (!terminateFlag.load(std::memory_order_relaxed)) {
        terminate();
    }
}

void PerfSyncThread::start(std::uint64_t monotonicRawBase)
{
    thread = std::thread {&PerfSyncThread::run, this, monotonicRawBase};
}

void PerfSyncThread::terminate()
{
    terminateFlag.store(true, std::memory_order_release);
    if (thread.joinable()) {
        thread.join();
    }
}

void PerfSyncThread::rename(std::uint64_t currentTime) const
{
    static constexpr std::size_t comm_size = 16;

    // we need a way to provoke a record to appear in the perf ring buffer that we can correlated back to an action here
    // rename thread which will generate a PERF_RECORD_COMM - encode the monotonic delta in uSeconds into the name
    // this allows us to work out ~ what the start time was relative to the local-clock event
    if (enableSyncThreadMode) {

        const std::uint64_t uSeconds = (currentTime) / NS_TO_US;
        if (uSeconds <= 9999999999ULL) {
            lib::printf_str_t<comm_size> buffer {"gds-%010u-", static_cast<unsigned>(uSeconds)};
            prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(buffer.c_str()), 0, 0, 0);
        }
        else {
            prctl(PR_SET_NAME, reinterpret_cast<unsigned long>("gator-sync-0"), 0, 0, 0);
        }
    }
}

void PerfSyncThread::run(std::uint64_t monotonicRawBase) noexcept
{
    // get pid and tid
    const pid_t pid = getpid();
    const pid_t tid = lib::gettid();

    // change thread priority
    set_thread_scheduling(tid);

    // Mask all signals so that this thread will not be woken up
    {
        sigset_t set;
        if (sigfillset(&set) != 0) {
            LOG_ERROR("sigfillset failed: %d (%s)", errno, lib::strerror());
            handleException();
        }
        if (sigprocmask(SIG_SETMASK, &set, nullptr) != 0) {
            LOG_ERROR("sigprocmask failed %d (%s)", errno, lib::strerror());
            handleException();
        }
    }

    // yield the thread so that we are on the correct cpu and to reduce the likelihood of yielding before sync starts
    sched_yield();

    // rename thread
    {
        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>("gator-sync-0"), 0, 0, 0);
    }

    // read CNTFREQ_EL0
    const std::uint64_t frequency = readTimer ? lib::get_cntfreq_el0() : 0;

    // main loop (always executes at least once to ensure we always capture at least one sync point
    do {
        // get current timestamp
        const std::uint64_t syncTime = getTime();

        // get architectural timer for SPE sync
        const std::uint64_t vcount = readTimer ? lib::get_cntvct_el0() : 0;

        // send the updated name with the monotonic delta
        rename(syncTime - monotonicRawBase);

        // send the data to the consumer
        consumerFunction(pid, tid, frequency, syncTime, vcount);

        // sleep for short period
        struct timespec ts;
        ts.tv_sec = NS_TO_SLEEP / NS_PER_S;
        ts.tv_nsec = NS_TO_SLEEP % NS_PER_S;
        if (nanosleep(&ts, nullptr) != 0) {
            LOG_ERROR("nanosleep failed: %d (%s)", errno, lib::strerror());
            handleException();
        }
    } while (!terminateFlag.load(std::memory_order_acquire));
}
