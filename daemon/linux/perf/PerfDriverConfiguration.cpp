/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfDriverConfiguration.h"

#include "Logging.h"
#include "PerfUtils.h"
#include "SessionData.h"
#include "k/perf_event.h"
#include "lib/FileDescriptor.h"
#include "lib/Format.h"
#include "lib/FsEntry.h"
#include "lib/Popen.h"
#include "lib/Syscall.h"
#include "lib/Utils.h"

#include <algorithm>
#include <set>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#define PERF_DEVICES "/sys/bus/event_source/devices"

constexpr int PerfDriverConfiguration::UNKNOWN_CPUID;
constexpr char PerfDriverConfiguration::ARMV82_SPE[];

static const std::string debugPerfEventMlockKbPropString = "debug.perf_event_mlock_kb";
static const std::string securityPerfHardenPropString = "security.perf_harden";

using lib::FsEntry;

static bool getPerfHarden()
{
    const char * const command[] = {"getprop", securityPerfHardenPropString.c_str(), nullptr};
    const lib::PopenResult getprop = lib::popen(command);
    if (getprop.pid < 0) {
        logg.logMessage("lib::popen(%s %s) failed: %s. Probably not android",
                        command[0],
                        command[1],
                        strerror(-getprop.pid));
        return false;
    }

    char value = '0';
    lib::readAll(getprop.out, &value, 1);
    lib::pclose(getprop);
    return value == '1';
}

static void setProp(const std::string & prop, const std::string & value)
{
    const char * const command[] = {"setprop", prop.c_str(), value.c_str(), nullptr};

    const lib::PopenResult setPropResult = lib::popen(command);
    //setprop not found, probably not Android.
    if (setPropResult.pid == -ENOENT) {
        logg.logMessage("lib::popen(%s %s %s) failed: %s",
                        command[0],
                        command[1],
                        command[2],
                        strerror(-setPropResult.pid));
        return;
    }
    if (setPropResult.pid < 0) {
        logg.logError("lib::popen(%s %s %s) failed: %s",
                      command[0],
                      command[1],
                      command[2],
                      strerror(-setPropResult.pid));
        return;
    }

    const int status = lib::pclose(setPropResult);
    if (!WIFEXITED(status)) {
        logg.logError("'%s %s %s' exited abnormally", command[0], command[1], command[2]);
        return;
    }

    const int exitCode = WEXITSTATUS(status);
    if (exitCode != 0) {
        logg.logError("'%s %s %s' failed: %d", command[0], command[1], command[2], exitCode);
    }
}

static void setPerfHarden(bool on)
{
    setProp(securityPerfHardenPropString, on ? "1" : "0");
}

static bool setPerfEventMlockKb(int newValue)
{
    lib::Optional<std::int64_t> fileValue = perf_utils::readPerfEventMlockKb();

    if (fileValue.valid() && fileValue.get() == newValue) {
        return true;
    }

    logg.logMessage("setting property %s to %d", debugPerfEventMlockKbPropString.c_str(), newValue);
    setProp(debugPerfEventMlockKbPropString, std::to_string(newValue));

    // Trigger debug property update
    setPerfHarden(false);

    // Give time for the debug property update to finish
    sleep(1);

    fileValue = perf_utils::readPerfEventMlockKb();
    const bool result = fileValue.valid() ? fileValue.get() == newValue : false;

    if (!result) {
        logg.logMessage("failed to set property %s to %d", debugPerfEventMlockKbPropString.c_str(), newValue);
    }

    return result;
}

static void setSuitablePerfEventMlockKbValue(int numCpus)
{
    // This function needs further consideration because "largeBufferSize" might be less than the default
    const int largeBufferSize = (1 + numCpus * 64) * (gSessionData.mPageSize / 1024);

    if (!setPerfEventMlockKb(largeBufferSize)) {
        const int smallerBufferSize = 129 * (gSessionData.mPageSize / 1024);

        setPerfEventMlockKb(smallerBufferSize);
    }
}

/**
 * @return true if perf harden in now off
 */
static bool disablePerfHarden()
{
    if (!getPerfHarden()) {
        return true;
    }

    logg.logWarning("disabling property %s", securityPerfHardenPropString.c_str());

    setPerfHarden(false);

    sleep(1);

    return !getPerfHarden();
}

static bool beginsWith(const char * string, const char * prefix)
{
    return strncmp(string, prefix, strlen(prefix)) == 0;
}

static bool isValidCpuId(int cpuId)
{
    return ((cpuId != PerfDriverConfiguration::UNKNOWN_CPUID) && (cpuId != -1) && (cpuId != 0));
}

void logCpuNotFound()
{
#if defined(__arm__) || defined(__aarch64__)
    logg.logSetup("CPU is not recognized\nUsing the Arm architected counters");
#else
    logg.logSetup("CPU is not recognized\nUsing perf hardware counters");
#endif
}

std::unique_ptr<PerfDriverConfiguration> PerfDriverConfiguration::detect(bool systemWide,
                                                                         const char * tracefsEventsPath,
                                                                         lib::Span<const int> cpuIds,
                                                                         const PmuXML & pmuXml)
{
    struct utsname utsname;
    if (lib::uname(&utsname) != 0) {
        logg.logError("uname failed");
        return nullptr;
    }

    logg.logMessage("Kernel version: %s", utsname.release);

    // Check the kernel version
    const int kernelVersion = lib::parseLinuxVersion(utsname);

    const bool hasArmv7PmuDriver = beginsWith(utsname.machine, "armv7")
        || FsEntry::create("/sys/bus/event_source/devices").hasChildWithNamePrefix("armv7");

#if CONFIG_PERF_SUPPORT_REGISTER_UNWINDING
    const bool has_64bit_register_set = beginsWith(utsname.machine, "aarch64") || beginsWith(utsname.machine, "arm64"); // use the machine name as allowed to run 32-bit gator on aarch64 machine
#else
    const bool has_64bit_register_set = (sizeof(void*) == 8);
#endif

    if (kernelVersion < KERNEL_VERSION(3, 4, 0)) {
        const char error[] = "Unsupported kernel version\nPlease upgrade to 3.4 or later";
        logg.logSetup(error);
        logg.logError(error);
        return nullptr;
    }

    const bool isRoot = (lib::geteuid() == 0);

    if (!isRoot && !disablePerfHarden()) {
        logg.logSetup("Failed to disable property %s\n" //
                      "Try 'adb shell setprop %s 0'",
                      securityPerfHardenPropString.c_str(),
                      securityPerfHardenPropString.c_str());
        logg.logError("Failed to disable property %s\n" //
                      "Try 'setprop %s 0' as the shell or root user.",
                      securityPerfHardenPropString.c_str(),
                      securityPerfHardenPropString.c_str());
        return nullptr;
    }

    int perf_event_paranoid;
    if (lib::readIntFromFile("/proc/sys/kernel/perf_event_paranoid", perf_event_paranoid) != 0) {
        if (isRoot) {
            const char error[] = "perf_event_paranoid not accessible\n"
                                 "Is CONFIG_PERF_EVENTS enabled?";
            logg.logSetup(error);
            logg.logError(error);
            return nullptr;
        }
#if defined(CONFIG_ASSUME_PERF_HIGH_PARANOIA) && CONFIG_ASSUME_PERF_HIGH_PARANOIA
        perf_event_paranoid = 2;
#else
        perf_event_paranoid = 1;
#endif
        logg.logSetup("perf_event_paranoid not accessible\nAssuming high paranoia (%d).", perf_event_paranoid);
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
        logg.logSetup("perf_event_open\nperf_event_paranoid > 2 is not supported for non-root");
        logg.logError("perf_event_open: perf_event_paranoid > 2 is not supported for non-root.\n"
                      "To use it try (as root):\n"
                      "  echo 2 > /proc/sys/kernel/perf_event_paranoid");
        return nullptr;
    }

    if (systemWide && !allow_system_wide) {
        logg.logSetup("System wide tracing\nperf_event_paranoid > 0 is not supported for system-wide non-root");
        logg.logError("perf_event_open: perf_event_paranoid > 0 is not supported for system-wide non-root.\n"
                      "To use it\n"
                      " * try --system-wide=no,\n"
                      " * run gatord as root,\n"
                      " * or make sure '/proc/sys/kernel/perf_event_paranoid' is set to -1.\n"
                      "   Try (as root):\n"
                      "    - echo -1 > /proc/sys/kernel/perf_event_paranoid");
        return nullptr;
    }

    const bool can_access_tracepoints = (lib::access(tracefsEventsPath, R_OK) == 0);
    const bool can_access_raw_tracepoints = can_access_tracepoints && (isRoot || perf_event_paranoid == -1);
    if (can_access_tracepoints) {
        logg.logMessage("Have access to tracepoints");
    }
    else {
        logg.logMessage("Don't have access to tracepoints");
    }

    // Must have tracepoints or perf_event_attr.context_switch for sched switch info
    if (systemWide && (!can_access_raw_tracepoints) && (kernelVersion < KERNEL_VERSION(4, 3, 0))) {
        if (can_access_tracepoints) {
            logg.logSetup("System wide tracing\nperf_event_paranoid > -1 is not supported for system-wide non-root");
            logg.logError("perf_event_open: perf_event_paranoid > -1 is not supported for system-wide non-root.\n"
                          "To use it\n"
                          " * try --system-wide=no,\n"
                          " * run gatord as root,\n"
                          " * or make sure '/proc/sys/kernel/perf_event_paranoid' is set to -1.\n"
                          "   Try (as root):\n"
                          "    - echo -1 > /proc/sys/kernel/perf_event_paranoid");
        }
        else {
            if (isRoot) {
                logg.logSetup("%s does not exist\nIs CONFIG_TRACING and CONFIG_CONTEXT_SWITCH_TRACER enabled?",
                              tracefsEventsPath);
                logg.logError("%s is not available.\n"
                              "Try:\n"
                              " - mount -t debugfs none /sys/kernel/debug",
                              tracefsEventsPath);
            }
            else {
                logg.logSetup("%s does not exist\nIs CONFIG_TRACING and CONFIG_CONTEXT_SWITCH_TRACER enabled?",
                              tracefsEventsPath);
                logg.logError("%s is not available.\n"
                              "Try:\n"
                              " * --system-wide=no,\n"
                              " * run gatord as root,\n"
                              " * or (as root):\n"
                              "    - mount -o remount,mode=755 /sys/kernel/debug\n"
                              "    - mount -o remount,mode=755 /sys/kernel/debug/tracing",
                              tracefsEventsPath);
            }
        }
        return nullptr;
    }

    // create the configuration object, from this point on perf is supported
    std::unique_ptr<PerfDriverConfiguration> configuration {new PerfDriverConfiguration()};

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
    configuration->config.can_access_tracepoints = can_access_raw_tracepoints;

    configuration->config.has_armv7_pmu_driver = hasArmv7PmuDriver;

    configuration->config.has_64bit_register_set = has_64bit_register_set;

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
            const GatorCpu * gatorCpu = pmuXml.findCpuByName(name);
            if (gatorCpu != nullptr) {
                int type;
                const std::string path(lib::Format() << PERF_DEVICES << "/" << name << "/type");
                if (lib::readIntFromFile(path.c_str(), type) == 0) {
                    logg.logMessage("    ... using pmu type %d for %s cores", type, gatorCpu->getCoreName());
                    configuration->cpus.push_back(PerfCpu {*gatorCpu, type});
                    cpusDetectedViaSysFs.insert(gatorCpu);
                    if (gatorCpu->getSpeName() != nullptr) {
                        haveFoundKnownCpuWithSpe = true;
                    }
                    continue;
                }
            }

            const UncorePmu * uncorePmu = pmuXml.findUncoreByName(name);
            if (uncorePmu != nullptr) {
                int type;
                const std::string path(lib::Format() << PERF_DEVICES << "/" << name << "/type");
                if (lib::readIntFromFile(path.c_str(), type) == 0) {
                    logg.logMessage("    ... is uncore pmu %s", uncorePmu->getCoreName());
                    configuration->uncores.push_back(PerfUncore {*uncorePmu, type});
                    continue;
                }
            }

            // handle generic pmu.

            // The generic pmu may be named "armv8_pmuv3", or where there are multiple of them, numbered like
            // "armv8_pmuv3_0", "armv8_pmuv3_1" etc.
            // This is found, for example, on the Juno board when booted from EFI without dtb.
            // In either case, attempt to read the "type" from the PMU and match it to a cluster
            // this is done by using the 'cpus' mask file and checking that all the matched CPUs have
            // the same CPUID.
            if (beginsWith(name, "armv8_pmuv3")) {
                int type;
                const std::string typePath(lib::Format() << PERF_DEVICES << "/" << name << "/type");
                const std::string maskPath(lib::Format() << PERF_DEVICES << "/" << name << "/cpus");
                if (lib::readIntFromFile(typePath.c_str(), type) == 0) {
                    const std::set<int> cpuNumbers = lib::readCpuMaskFromFile(maskPath.c_str());
                    if (!cpuNumbers.empty()) {
                        int cpuIdForType = 0;
                        for (int cpuNumber : cpuNumbers) {
                            // track generic pmu's type to the cpuId associated with it.
                            // if multiple different cpuIds are associated, then use -1 as cannot map to a unique pmu.
                            const int cpuIdForCpu = cpuIds[cpuNumber];
                            logg.logMessage("    ... cpu %d, with cpuid 0x%05x", cpuNumber, cpuIdForCpu);
                            if (!isValidCpuId(cpuIdForCpu)) {
                                // skip it as we don't know what it is. fair to assume
                                // homogeneous clusters.
                                continue;
                            }
                            if (cpuIdForType == 0) {
                                cpuIdForType = cpuIdForCpu;
                            }
                            else if (cpuIdForType != cpuIdForCpu) {
                                cpuIdForType = -1;
                            }
                        }
                        if (isValidCpuId(cpuIdForType)) {
                            const GatorCpu * gatorCpu = pmuXml.findCpuById(cpuIdForType);
                            if (gatorCpu != nullptr) {
                                logg.logMessage("    ... using generic pmu type %d for %s cores",
                                                type,
                                                gatorCpu->getCoreName());
                                configuration->cpus.push_back(PerfCpu {*gatorCpu, type});
                                cpusDetectedViaSysFs.insert(gatorCpu);
                                if (gatorCpu->getSpeName() != nullptr) {
                                    haveFoundKnownCpuWithSpe = true;
                                }
                            }
                        }
                    }
                    continue;
                }
            }

            // detect spe pmu
            if (beginsWith(name, "arm_spe_")) {
                int type;
                const std::string typePath(lib::Format() << PERF_DEVICES << "/" << name << "/type");
                const std::string maskPath(lib::Format() << PERF_DEVICES << "/" << name << "/cpumask");
                if (lib::readIntFromFile(typePath.c_str(), type) == 0) {
                    const std::set<int> cpuNumbers = lib::readCpuMaskFromFile(maskPath.c_str());
                    for (int cpuNumber : cpuNumbers) {
                        logg.logMessage("    ... using SPE pmu type %d for cpu %d", type, cpuNumber);
                        configuration->cpuNumberToSpeType[cpuNumber] = type;
                    }
                    continue;
                }
            }
        }
    }
    else {
        logg.logMessage(PERF_DEVICES " doesn't exist");
    }

    // additionally add any by CPUID
    std::set<int> unrecognisedCpuIds;
    for (int cpuId : cpuIds) {
        const GatorCpu * gatorCpu = pmuXml.findCpuById(cpuId);
        if (gatorCpu == nullptr) {
            // track the unknown cpuid (filter out some junk values)
            if (isValidCpuId(cpuId)) {
                unrecognisedCpuIds.insert(cpuId);
            }
        }
        else if ((cpusDetectedViaSysFs.count(gatorCpu) == 0) && (cpusDetectedViaCpuid.count(gatorCpu) == 0)) {
            logg.logMessage("generic pmu: %s", gatorCpu->getCoreName());
            configuration->cpus.push_back(PerfCpu {*gatorCpu, PERF_TYPE_RAW});
            cpusDetectedViaCpuid.insert(gatorCpu);
            if (gatorCpu->getSpeName() != nullptr) {
                haveFoundKnownCpuWithSpe = true;
            }
        }
    }

    //if CPUs are incorrect up until this point.
    //If the kernel has detected v7 cores when the cores are v8 due to kernel running 32 bit.
    bool anyV7 = false;
    bool anyV8 = false;

    for (unsigned int i = 0; i < configuration->cpus.size(); ++i) {
        anyV7 |= !configuration->cpus[i].gator_cpu.getIsV8();
        anyV8 |= configuration->cpus[i].gator_cpu.getIsV8();

        if (anyV7 && anyV8) {
            //the clusters are mixed, therefore remove all cpus that arent v8.
            configuration->cpus.erase(std::remove_if(configuration->cpus.begin(),
                                                     configuration->cpus.end(),
                                                     [](const PerfCpu & cpu) { return !(cpu.gator_cpu.getIsV8()); }),
                                      configuration->cpus.end());
            break;
        }
    }

    const bool hasNoCpus = cpusDetectedViaSysFs.empty() && cpusDetectedViaCpuid.empty();
    const bool haveUnknownSpe = !configuration->cpuNumberToSpeType.empty() && !haveFoundKnownCpuWithSpe;
    const bool addOtherForUnknownSpe = (haveUnknownSpe && configuration->cpus.empty());

    // need to update or create a record to set the SPE flag?
    if (haveUnknownSpe && !configuration->cpus.empty()) {
        for (auto & cpu : configuration->cpus) {
            const auto & currentValue = cpu;
            cpu = PerfCpu {GatorCpu(currentValue.gator_cpu, ARMV82_SPE), currentValue.pmu_type};
        }
    }

    // insert 'Other' for unknown CPUIDs / no cpus found
    if ((hasNoCpus || addOtherForUnknownSpe) && unrecognisedCpuIds.empty()) {
        unrecognisedCpuIds.insert(UNKNOWN_CPUID);
    }

    if (!unrecognisedCpuIds.empty()) {
        logCpuNotFound();
        const char * const speName = (addOtherForUnknownSpe ? ARMV82_SPE : nullptr);

#if defined(__aarch64__)
        configuration->cpus.push_back(
            PerfCpu {{"Other", "Other", "Other", nullptr, speName, unrecognisedCpuIds, 6, true}, PERF_TYPE_RAW});
#elif defined(__arm__)
        configuration->cpus.push_back(
            PerfCpu {{"Other", "Other", "Other", nullptr, speName, unrecognisedCpuIds, 6, anyV8}, PERF_TYPE_RAW});
#else
        configuration->cpus.push_back(
            PerfCpu {{"Other", "Perf_Hardware", "Perf_Hardware", nullptr, speName, unrecognisedCpuIds, 6, false},
                     PERF_TYPE_HARDWARE});
#endif
    }

    if (cpusDetectedViaSysFs.empty() && !cpusDetectedViaCpuid.empty() && (dir.exists())) {
        logg.logSetup(
            "No Perf PMUs detected\n"
            "Could not detect any Perf PMUs in /sys/bus/event_source/devices/ but the system contains recognised CPUs. "
            "The system may not support perf hardware counters. Check CONFIG_HW_PERF_EVENTS is set and that the PMU is "
            "configured in the target device tree.");
    }

    setSuitablePerfEventMlockKbValue(cpuIds.size());

    return configuration;
}
