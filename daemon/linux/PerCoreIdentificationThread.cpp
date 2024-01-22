/* Copyright (C) 2018-2023 by Arm Limited. All rights reserved. */

#include "linux/PerCoreIdentificationThread.h"

#include "Logging.h"
#include "lib/String.h"
#include "lib/Syscall.h"
#include "lib/Utils.h"
#include "linux/CoreOnliner.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <optional>
#include <set>
#include <utility>

#include <sys/prctl.h>
#include <sys/types.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#include <sched.h>
#undef _GNU_SOURCE
#else
#include <sched.h>
#endif

PerCoreIdentificationThread::PerCoreIdentificationThread(bool ignoreOffline,
                                                         unsigned cpu,
                                                         ConsumerFunction consumerFunction)
    : consumerFunction(std::move(consumerFunction)), cpu(cpu), ignoreOffline(ignoreOffline), thread(launch, this)
{
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

    const pid_t tid = lib::gettid();

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);

    // try and set affinity
    bool affinitySucceeded = false;
    for (unsigned count = 0; count < AFFINE_LOOP_COUNT && !affinitySucceeded; ++count) {
        if (sched_setaffinity(tid, sizeof(cpu_set_t), &cpuset) == 0) {
            affinitySucceeded = true;
        }
    }

    if (!affinitySucceeded) {
        //NOLINTNEXTLINE(concurrency-mt-unsafe)
        LOG_WARNING("Error calling sched_setaffinity on %u: %d (%s)", cpu, errno, strerror(errno));
        return false;
    }

    // sched_setaffinity only updates the CPU mask associated with the thread, it doesn't do the migration
    // so yield the thread so that we are on the correct cpu
    sched_yield();

    return true;
}

void PerCoreIdentificationThread::run() noexcept
{
    std::optional<CoreOnliner> coreOnliner;

    // rename thread
    {
        lib::printf_str_t<16> buffer {"gatord-cid-%d", cpu};
        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(buffer.c_str()), 0, 0, 0);
    }

    if (!ignoreOffline) {
        // attempt to read the online state of the core and then set it online
        coreOnliner = CoreOnliner(cpu);
        // affine the thread to a single CPU
        configureAffinity();
    }

    // inform callback (this is done regardless of whether or not configureAffinity succeeded
    // so that the function using these threads will be notified when each per-core thread has completed its work
    consumerFunction(cpu, detectFor(cpu));

    // reading MIDR_EL1 is supported via emulation from 4.11 on arm64 only
    // reading MIDR_EL1 is supported via sysfs from 4.8 on arm64 only and the sysfs filesystem is not always available (e.g. on Android)
    // sadly this means instead once all threads are running (and thus all cores are online) we read /proc/cpuinfo to get the CPUID info
    // hence the spin wait *after* the callback
    while (!terminatedFlag) {
        sched_yield();
    }
}

PerCoreIdentificationThread::properties_t PerCoreIdentificationThread::detectFor(unsigned cpu)
{
    bool core_id_valid = false;
    bool physical_package_id_valid = false;
    bool midr_el1_valid = false;
    int core_id = 0;
    int physical_package_id = 0;
    int64_t midr_el1 = 0;
    std::set<int> core_siblings;

    // attempt to read topology and identification information
    {
        // read topology information from sysfs if available
        lib::printf_str_t<128> buffer {"/sys/devices/system/cpu/cpu%u/topology/core_id", cpu};
        core_id_valid = (lib::readIntFromFile(buffer, core_id) == 0);

        buffer.printf("/sys/devices/system/cpu/cpu%u/topology/cluster_id", cpu);
        physical_package_id_valid = (lib::readIntFromFile(buffer, physical_package_id) == 0);

        if (!physical_package_id_valid) {
            buffer.printf("/sys/devices/system/cpu/cpu%u/topology/physical_package_id", cpu);
            physical_package_id_valid = (lib::readIntFromFile(buffer, physical_package_id) == 0);
        }

        buffer.printf("/sys/devices/system/cpu/cpu%u/topology/cluster_cpus_list", cpu);
        core_siblings = lib::readCpuMaskFromFile(buffer);

        if (core_siblings.empty()) {
            buffer.printf("/sys/devices/system/cpu/cpu%u/topology/core_siblings_list", cpu);
            core_siblings = lib::readCpuMaskFromFile(buffer);
        }

        // read MIDR value if available
        buffer.printf("/sys/devices/system/cpu/cpu%u/regs/identification/midr_el1", cpu);
        midr_el1_valid = (lib::readInt64FromFile(buffer, midr_el1) == 0);
    }

    return {
        (core_id_valid ? core_id : INVALID_CORE_ID),
        (physical_package_id_valid ? physical_package_id : INVALID_PACKAGE_ID),
        std::move(core_siblings),
        (midr_el1_valid ? midr_el1 : INVALID_MIDR_EL1),
    };
}
