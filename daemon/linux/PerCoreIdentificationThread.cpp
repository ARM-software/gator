/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#include "linux/PerCoreIdentificationThread.h"

#include "Logging.h"
#include "lib/Assert.h"
#include "lib/Optional.h"
#include "lib/Utils.h"
#include "linux/CoreOnliner.h"

#include <cstdio>
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

PerCoreIdentificationThread::PerCoreIdentificationThread(bool ignoreOffline, unsigned cpu, ConsumerFunction consumerFunction)
        : thread(),
          consumerFunction(consumerFunction),
          terminatedFlag(false),
          cpu(cpu),
          ignoreOffline(ignoreOffline)
{
    thread = std::thread(launch, this);
}

PerCoreIdentificationThread::~PerCoreIdentificationThread()
{
    terminatedFlag = true;
    thread.join();
}

void PerCoreIdentificationThread::launch(PerCoreIdentificationThread * _this) noexcept
{
    _this->run();
}

bool PerCoreIdentificationThread::configureAffinity()
{
    // the maximum number of times we will attempt to affine to the core before bailing
    static constexpr unsigned AFFINE_LOOP_COUNT = 65535;

    const pid_t tid = syscall(__NR_gettid);

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);

    // try and set affinity
    bool affinitySucceeded = false;
    for (unsigned count = 0; count < AFFINE_LOOP_COUNT && !affinitySucceeded; ++count)
    {
        if (sched_setaffinity(tid, sizeof(cpu_set_t), &cpuset) == 0) {
            affinitySucceeded = true;
        }
    }

    if (!affinitySucceeded) {
        logg.logMessage("Error calling sched_setaffinity on %u: %d (%s)", cpu, errno, strerror(errno));
        return false;
    }

    // change thread priority
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (sched_setscheduler(tid, SCHED_FIFO | SCHED_RESET_ON_FORK, &param) != 0) {
        logg.logMessage("Unable to schedule sync thread as FIFO, trying OTHER: %d (%s)", errno, strerror(errno));
        param.sched_priority = sched_get_priority_max(SCHED_OTHER);
        if (sched_setscheduler(tid, SCHED_OTHER | SCHED_RESET_ON_FORK, &param) != 0) {
            logg.logMessage("sched_setscheduler failed for %u: %d (%s)", cpu, errno, strerror(errno));
        }
    }

    // sched_setaffinity only updates the CPU mask associated with the thread, it doesn't do the migration
    // so yield the thread so that we are on the correct cpu
    sched_yield();

    return true;
}

void PerCoreIdentificationThread::run() noexcept
{
    lib::Optional<CoreOnliner> coreOnliner;
    bool core_id_valid = false;
    bool physical_package_id_valid = false;
    bool midr_el1_valid = false;
    int core_id = 0;
    int physical_package_id = 0;
    int64_t midr_el1 = 0;
    std::set<int> core_siblings;

    if (!ignoreOffline) {
        // attempt to read the online state of the core and then set it online
        coreOnliner.set(CoreOnliner(cpu));
        // affine the thread to a single CPU
        configureAffinity();
    }

    // attempt to read topology and identification information
    {
        // read topology information from sysfs if available
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "/sys/devices/system/cpu/cpu%u/topology/core_id", cpu);
        core_id_valid = (lib::readIntFromFile(buffer, core_id) == 0);

        snprintf(buffer, sizeof(buffer), "/sys/devices/system/cpu/cpu%u/topology/physical_package_id", cpu);
        physical_package_id_valid = (lib::readIntFromFile(buffer, physical_package_id) == 0);

        snprintf(buffer, sizeof(buffer), "/sys/devices/system/cpu/cpu%u/topology/core_siblings_list", cpu);
        core_siblings = lib::readCpuMaskFromFile(buffer);

        // read MIDR value if available
        snprintf(buffer, sizeof(buffer), "/sys/devices/system/cpu/cpu%u/regs/identification/midr_el1", cpu);
        midr_el1_valid = (lib::readInt64FromFile(buffer, midr_el1) == 0);
    }

    // inform callback (this is done regardless of whether or not configureAffinity succeeded
    // so that the function using these threads will be notified when each per-core thread has completed its work
    consumerFunction(cpu,
                     (core_id_valid ? core_id : INVALID_CORE_ID),
                     (physical_package_id_valid ? physical_package_id : INVALID_PACKAGE_ID),
                     core_siblings,
                     (midr_el1_valid ? midr_el1 : INVALID_MIDR_EL1));

    // reading MIDR_EL1 is supported via emulation from 4.11 on arm64 only
    // reading MIDR_EL1 is supported via sysfs from 4.8 on arm64 only and the sysfs filesystem is not always available (e.g. on Android)
    // sadly this means instead once all threads are running (and thus all cores are online) we read /proc/cpuinfo to get the CPUID info
    // hence the spin wait *after* the callback
    while (!terminatedFlag) {
       sched_yield();
    }
}
