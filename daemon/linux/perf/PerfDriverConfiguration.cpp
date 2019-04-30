/**
 * Copyright (C) Arm Limited 2013-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "linux/perf/PerfDriverConfiguration.h"

#include <sys/wait.h>
#include <unistd.h>
#include <sys/utsname.h>

#include <set>
#include <algorithm>

#include "k/perf_event.h"

#include "DriverSource.h"
#include "Logging.h"
#include "SessionData.h"
#include "lib/FileDescriptor.h"
#include "lib/Format.h"
#include "lib/FsEntry.h"
#include "lib/Utils.h"
#include "lib/Popen.h"
#include "lib/Syscall.h"

#define PERF_DEVICES "/sys/bus/event_source/devices"

using lib::FsEntry;

static bool getPerfHarden()
{
    const char * const command[] = { "getprop", "security.perf_harden", nullptr };
    const lib::PopenResult getprop = lib::popen(command);
    if (getprop.pid < 0) {
        logg.logMessage("lib::popen(%s %s) failed: %s. Probably not android", command[0], command[1], strerror(-getprop.pid));
        return false;
    }

    char value = '0';
    lib::readAll(getprop.out, &value, 1);
    lib::pclose(getprop);
    return value == '1';
}

static void setPerfHarden(bool on)
{
    const char* const command[] = { "setprop", "security.perf_harden", on ? "1" : "0", nullptr };

    const lib::PopenResult setprop = lib::popen(command);
    if (setprop.pid < 0) {
        logg.logError("lib::popen(%s %s %s) failed: %s", command[0], command[1], command[2], strerror(-setprop.pid));
        return;
    }

    const int status = lib::pclose(setprop);
    if (!WIFEXITED(status)) {
        logg.logError("'%s %s %s' exited abnormally", command[0], command[1], command[2]);
        return;
    }

    const int exitCode = WEXITSTATUS(status);
    if (exitCode != 0) {
        logg.logError("'%s %s %s' failed: %d", command[0], command[1], command[2], exitCode);
    }
}

/**
 * @return true if perf harden in now off
 */
static bool disablePerfHarden()
{
    if (!getPerfHarden())
        return true;

    logg.logWarning("disabling property security.perf_harden");

    setPerfHarden(false);

    sleep(1);

    return !getPerfHarden();
}

static bool beginsWith(const char* string, const char* prefix)
{
    return strncmp(string, prefix, strlen(prefix)) == 0;
}

void logCpuNotFound()
{
#if defined(__arm__) || defined(__aarch64__)
    logg.logSetup("CPU is not recognized\nUsing the Arm architected counters");
#else
    logg.logSetup("CPU is not recognized\nUsing perf hardware counters");
#endif
}

std::unique_ptr<PerfDriverConfiguration> PerfDriverConfiguration::detect(bool systemWide, lib::Span<const int> cpuIds, const PmuXML & pmuXml)
{
    struct utsname utsname;
    if (lib::uname(&utsname) != 0) {
        logg.logError("uname failed");
        return nullptr;
    }

    // Check the kernel version
    const int kernelVersion = lib::parseLinuxVersion(utsname);

    const bool hasArmv7PmuDriver = beginsWith(utsname.machine, "arm") && !beginsWith(utsname.machine, "arm64")
            && !beginsWith(utsname.machine, "armv6");

    if (kernelVersion < KERNEL_VERSION(3, 4, 0)) {
        logg.logSetup("Unsupported kernel version\nPlease upgrade to 3.4 or later");
        return nullptr;
    }

    const bool isRoot = (lib::geteuid() == 0);

    if (!isRoot && !disablePerfHarden()) {
        logg.logSetup("failed to disable property security.perf_harden\n" //
                "try 'adb shell setprop security.perf_harden 0'");
        return nullptr;
    }

    int perf_event_paranoid;
    if (lib::readIntFromFile("/proc/sys/kernel/perf_event_paranoid", perf_event_paranoid) != 0) {
        if (isRoot) {
            logg.logSetup("perf_event_paranoid not accessible\nIs CONFIG_PERF_EVENTS enabled?");
            return nullptr;
        }
        else {
            logg.logSetup("perf_event_paranoid not accessible\nAssuming high paranoia.");
            perf_event_paranoid = 2;
        }
    }
    else {
        logg.logMessage("perf_event_paranoid: %d", perf_event_paranoid);
    }

    const bool allow_system_wide = isRoot || perf_event_paranoid <= 0;
    const bool exclude_kernel = !(isRoot || perf_event_paranoid <= 1);
    const bool allow_non_system_wide = isRoot || perf_event_paranoid <= 2;

    if (!allow_non_system_wide) {
        // This is only actually true if the kernel has the grsecurity PERF_HARDEN patch
        // but we assume no-one would ever set perf_event_paranoid > 2 without it.
        logg.logSetup("perf_event_open\nperf_event_paranoid > 2 not supported for non-root");
        return nullptr;
    }

    if (systemWide && !allow_system_wide) {
        logg.logSetup("System wide tracing\nperf_event_paranoid > 0 not supported for system-wide non-root");
        return nullptr;
    }

    const bool can_access_tracepoints = (access(EVENTS_PATH, R_OK) == 0) && (isRoot || perf_event_paranoid == -1);
    if (can_access_tracepoints)
    {
        logg.logMessage("Have access to tracepoints");
    }
    else
    {
        logg.logMessage("Don't have access to tracepoints");
    }

    // Must have tracepoints or perf_event_attr.context_switch for sched switch info
    if (systemWide && (!can_access_tracepoints) && (kernelVersion < KERNEL_VERSION(4, 3, 0))) {
        logg.logSetup(EVENTS_PATH " does not exist\nIs CONFIG_TRACING and CONFIG_CONTEXT_SWITCH_TRACER enabled?");
        return nullptr;
    }

    // create the configuration object, from this point on perf is supported
    std::unique_ptr<PerfDriverConfiguration> configuration { new PerfDriverConfiguration() };

    configuration->config.has_fd_cloexec = (kernelVersion >= KERNEL_VERSION(3, 14, 0));
    configuration->config.has_count_sw_dummy = (kernelVersion >= KERNEL_VERSION(3, 12, 0));
    configuration->config.has_sample_identifier = (kernelVersion >= KERNEL_VERSION(3, 12, 0));
    configuration->config.has_attr_comm_exec = (kernelVersion >= KERNEL_VERSION(3, 16, 0));
    configuration->config.has_attr_mmap2 = (kernelVersion >= KERNEL_VERSION(3, 16, 0));
    configuration->config.has_attr_clockid_support = (kernelVersion >= KERNEL_VERSION(4, 1, 0));
    configuration->config.has_attr_context_switch = (kernelVersion >= KERNEL_VERSION(4, 3, 0));
    configuration->config.has_ioctl_read_id = (kernelVersion >= KERNEL_VERSION(3, 12, 0));
    configuration->config.has_aux_support = (kernelVersion >= KERNEL_VERSION(4, 1, 0));

    configuration->config.is_system_wide = systemWide;
    configuration->config.exclude_kernel = exclude_kernel;
    configuration->config.allow_system_wide = allow_system_wide;
    configuration->config.can_access_tracepoints = can_access_tracepoints;

    configuration->config.has_armv7_pmu_driver = hasArmv7PmuDriver;

    // detect the PMUs
    std::set<const GatorCpu *> cpusDetectedViaSysFs;
    std::set<const GatorCpu *> cpusDetectedViaCpuid;
    bool haveFoundKnownCpuWithSpe = false;
    // Add supported PMUs
    FsEntry dir = FsEntry::create(PERF_DEVICES);
    if (dir.exists()) {
        auto children = dir.children();
        lib::Optional<FsEntry> dirent;
        while ((dirent = children.next())) {
            const std::string nameString = dirent.get().name();
            const char * const name = nameString.c_str();
            logg.logMessage("perf pmu: %s", name);
            const GatorCpu *gatorCpu = pmuXml.findCpuByName(name);
            if (gatorCpu != NULL) {
                int type;
                const std::string path (lib::Format() << PERF_DEVICES << "/" << name << "/type");
                if (lib::readIntFromFile(path.c_str(), type) == 0) {
                    configuration->cpus.push_back(PerfCpu { *gatorCpu, type });
                    cpusDetectedViaSysFs.insert(gatorCpu);
                    if (gatorCpu->getSpeName() != nullptr)
                        haveFoundKnownCpuWithSpe = true;
                    continue;
                }
            }

            const UncorePmu * uncorePmu = pmuXml.findUncoreByName(name);
            if (uncorePmu != NULL) {
                int type;
                const std::string path (lib::Format() << PERF_DEVICES << "/" << name << "/type");
                if (lib::readIntFromFile(path.c_str(), type) == 0) {
                    configuration->uncores.push_back(PerfUncore { *uncorePmu, type });
                    continue;
                }
            }

            if (beginsWith(name, "arm_spe_")) {
                int type;
                const std::string typePath (lib::Format() << PERF_DEVICES << "/" << name << "/type");
                const std::string maskPath (lib::Format() << PERF_DEVICES << "/" << name << "/cpumask");
                if (lib::readIntFromFile(typePath.c_str(), type) == 0) {
                    const std::set<int> cpuNumbers = lib::readCpuMaskFromFile(maskPath.c_str());
                    for (int cpuNumber : cpuNumbers) {
                        configuration->cpuNumberToSpeType[cpuNumber] = type;
                    }
                    continue;
                }
            }
        }
    }
    else
        logg.logMessage(PERF_DEVICES " doesn't exist");

    // additionally add any by CPUID
    for (int cpuId: cpuIds) {
        const GatorCpu *gatorCpu = pmuXml.findCpuById(cpuId);
        if ((gatorCpu != NULL) && (cpusDetectedViaSysFs.count(gatorCpu) == 0) && (cpusDetectedViaCpuid.count(gatorCpu) == 0)) {
            logg.logMessage("generic pmu: %s", gatorCpu->getCoreName());
            configuration->cpus.push_back(PerfCpu { *gatorCpu, PERF_TYPE_RAW });
            cpusDetectedViaCpuid.insert(gatorCpu);
            if (gatorCpu->getSpeName() != nullptr)
                haveFoundKnownCpuWithSpe = true;
        }
    }

    //if CPUs are incorrect up until this point.
    //If the kernel has detected v7 cores when the cores are v8 due to kernel running 32 bit.
    bool previous = false;

    for (unsigned int i = 0; i < configuration->cpus.size(); ++i)
    {
        if ((configuration->cpus[i].gator_cpu.getIsV8() != previous) && (i > 0)) {
            //the clusters are mixed, therefore remove all cpus that arent v8.
            configuration->cpus.erase(
                    std::remove_if(configuration->cpus.begin(), configuration->cpus.end(),
                                   [](const PerfCpu & cpu) {return !(cpu.gator_cpu.getIsV8());}),
                    configuration->cpus.end());
        }
        previous = configuration->cpus[i].gator_cpu.getIsV8();
    }

    // force add other
    const bool haveUnkonwnCpu = cpusDetectedViaSysFs.empty() && cpusDetectedViaCpuid.empty();
    const bool haveUnknownSpe = !configuration->cpuNumberToSpeType.empty() && !haveFoundKnownCpuWithSpe;
    if (haveUnkonwnCpu || haveUnknownSpe) {
        logCpuNotFound();

        const char * const speName = haveUnknownSpe ? "armv8.2_spe" : nullptr;
#if defined(__aarch64__)
        configuration->cpus.push_back(PerfCpu { {"Other", "Other", nullptr, speName, UNKNOWN_CPUID, 6, true}, PERF_TYPE_RAW});
#elif defined(__arm__)
        configuration->cpus.push_back(PerfCpu { {"Other", "Other", nullptr, speName, UNKNOWN_CPUID, 6, false}, PERF_TYPE_RAW});
#else
        configuration->cpus.push_back(
                PerfCpu { { "Other", "Perf_Hardware", nullptr, speName, UNKNOWN_CPUID, 6, false },
                                                PERF_TYPE_HARDWARE });
#endif
    }

    if (cpusDetectedViaSysFs.empty() && !cpusDetectedViaCpuid.empty() && (dir.exists())) {
        logg.logSetup("No Perf PMUs detected\n"
                      "Could not detect any Perf PMUs in /sys/bus/event_source/devices/ but the system contains recognised CPUs. "
                      "The system may not support perf hardware counters. Check CONFIG_HW_PERF_EVENTS is set and that the PMU is configured in the target device tree.");
    }

    return configuration;
}

