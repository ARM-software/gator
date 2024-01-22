/* Copyright (C) 2013-2023 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfDriverConfiguration.h"

#include "Config.h"
#include "Logging.h"
#include "SessionData.h"
#include "capture/Environment.h"
#include "k/perf_event.h"
#include "lib/FileDescriptor.h"
#include "lib/Format.h"
#include "lib/FsEntry.h"
#include "lib/Popen.h"
#include "lib/Span.h"
#include "lib/Syscall.h"
#include "lib/Utils.h"
#include "linux/smmu_identifier.h"
#include "linux/smmu_support.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include <sys/utsname.h>
#include <unistd.h>

#define PERF_DEVICES "/sys/bus/event_source/devices"

static constexpr std::string_view securityPerfHardenPropString = "security.perf_harden";

using namespace gator::smmuv3;
using lib::FsEntry;

static bool getPerfHarden()
{
    const char * const command[] = {"getprop", securityPerfHardenPropString.data(), nullptr};
    const lib::PopenResult getprop = lib::popen(command);
    if (getprop.pid < 0) {
        LOG_DEBUG("lib::popen(%s %s) failed: %s. Probably not android", command[0], command[1], strerror(-getprop.pid));
        return false;
    }

    char value = '0';
    lib::readAll(getprop.out, &value, 1);
    lib::pclose(getprop);
    return value == '1';
}

static void setProp(std::string_view prop, const std::string & value)
{
    const char * const command[] = {"setprop", prop.data(), value.c_str(), nullptr};

    const lib::PopenResult setPropResult = lib::popen(command);
    //setprop not found, probably not Android.
    if (setPropResult.pid == -ENOENT) {
        LOG_DEBUG("lib::popen(%s %s %s) failed: %s", command[0], command[1], command[2], strerror(-setPropResult.pid));
        return;
    }
    if (setPropResult.pid < 0) {
        LOG_ERROR("lib::popen(%s %s %s) failed: %s", command[0], command[1], command[2], strerror(-setPropResult.pid));
        return;
    }

    const int status = lib::pclose(setPropResult);
    if (!WIFEXITED(status)) {
        LOG_ERROR("'%s %s %s' exited abnormally", command[0], command[1], command[2]);
        return;
    }

    const int exitCode = WEXITSTATUS(status);
    if (exitCode != 0) {
        LOG_ERROR("'%s %s %s' failed: %d", command[0], command[1], command[2], exitCode);
    }
}

static void setPerfHarden(bool on)
{
    setProp(securityPerfHardenPropString, on ? "1" : "0");
}

/**
 * @return true if perf harden in now off
 */
static bool disablePerfHarden()
{
    if (!getPerfHarden()) {
        return true;
    }

    LOG_WARNING("disabling property %s", securityPerfHardenPropString.data());

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
    LOG_SETUP("CPU is not recognized\nUsing the Arm architected counters");
#else
    LOG_SETUP("CPU is not recognized\nUsing perf hardware counters");
#endif
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::unique_ptr<PerfDriverConfiguration> PerfDriverConfiguration::detect(bool systemWide,
                                                                         const char * tracefsEventsPath,
                                                                         lib::Span<const int> cpuIds,
                                                                         const default_identifiers_t & smmu_identifiers,
                                                                         const PmuXML & pmuXml)
{
    struct utsname utsname;
    if (lib::uname(&utsname) != 0) {
        LOG_ERROR("uname failed");
        return nullptr;
    }

    LOG_DEBUG("Kernel version: %s", utsname.release);

    // Check the kernel version
    auto const kernelVersion = lib::parseLinuxVersion(utsname);

    const bool hasArmv7PmuDriver = beginsWith(utsname.machine, "armv7")
                                || FsEntry::create("/sys/bus/event_source/devices").hasChildWithNamePrefix("armv7");

#if CONFIG_PERF_SUPPORT_REGISTER_UNWINDING
    const bool has_64bit_uname =
        beginsWith(utsname.machine, "aarch64") || beginsWith(utsname.machine, "arm64")
        || beginsWith(utsname.machine,
                      "armv8"); // use the machine name as allowed to run 32-bit gator on aarch64 machine
#else
    const bool has_64bit_uname = (sizeof(void *) == 8);
#endif

    const bool use_64bit_register_set = (sizeof(void *) == 8) || has_64bit_uname;

    if (kernelVersion < KERNEL_VERSION(3U, 4U, 0U)) {
        const char error[] = "Unsupported kernel version\nPlease upgrade to 3.4 or later";
        LOG_SETUP(error);
        LOG_ERROR(error);
        return nullptr;
    }

    const auto os_type = capture::detectOs();
    const bool is_android = (os_type == capture::OsType::Android);

    const bool isRoot = (lib::geteuid() == 0);

    if (is_android && !isRoot && !disablePerfHarden()) {
        LOG_SETUP("Failed to disable property %s\n" //
                  "Try 'adb shell setprop %s 0'",
                  securityPerfHardenPropString.data(),
                  securityPerfHardenPropString.data());
        LOG_ERROR("Failed to disable property %s\n" //
                  "Try 'setprop %s 0' as the shell or root user.",
                  securityPerfHardenPropString.data(),
                  securityPerfHardenPropString.data());
        return nullptr;
    }

    int perf_event_paranoid;
    if (lib::readIntFromFile("/proc/sys/kernel/perf_event_paranoid", perf_event_paranoid) != 0) {
        if (isRoot) {
            const char error[] = "perf_event_paranoid not accessible\n"
                                 "Is CONFIG_PERF_EVENTS enabled?";
            LOG_SETUP(error);
            LOG_ERROR(error);
            return nullptr;
        }
#if defined(CONFIG_ASSUME_PERF_HIGH_PARANOIA) && CONFIG_ASSUME_PERF_HIGH_PARANOIA
        perf_event_paranoid = 2;
#else
        perf_event_paranoid = 1;
#endif
        LOG_SETUP("perf_event_paranoid not accessible\nAssuming high paranoia (%d).", perf_event_paranoid);
    }
    else {
        LOG_DEBUG("perf_event_paranoid: %d", perf_event_paranoid);
    }

    const bool can_collect_system_wide_data = isRoot || perf_event_paranoid <= 0;
    const bool can_collect_kernel_data = isRoot || (perf_event_paranoid <= 1);
    const bool can_collect_any_data = isRoot || perf_event_paranoid <= 2;

    if (!can_collect_any_data) {
        // This is only actually true if the kernel has the grsecurity PERF_HARDEN patch
        // but we assume no-one would ever set perf_event_paranoid > 2 without it.
        LOG_SETUP("perf_event_open\nperf_event_paranoid > 2 is not supported for non-root");
        LOG_ERROR("perf_event_open: perf_event_paranoid > 2 is not supported for non-root.\n"
                  "To use it try (as root):\n"
                  "  echo 2 > /proc/sys/kernel/perf_event_paranoid");
        return nullptr;
    }

    if (systemWide && !can_collect_system_wide_data) {
        LOG_SETUP("System wide tracing\nperf_event_paranoid > 0 is not supported for system-wide non-root");
        LOG_ERROR("perf_event_open: perf_event_paranoid > 0 is not supported for system-wide non-root.\n"
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
        LOG_DEBUG("Have access to tracepoints");
    }
    else {
        LOG_DEBUG("Don't have access to tracepoints");
    }

    // Must have tracepoints or perf_event_attr.context_switch for sched switch info
    if (systemWide && (!can_access_raw_tracepoints) && (kernelVersion < KERNEL_VERSION(4U, 3U, 0U))) {
        if (can_access_tracepoints) {
            LOG_SETUP("System wide tracing\nperf_event_paranoid > -1 is not supported for system-wide non-root");
            LOG_ERROR("perf_event_open: perf_event_paranoid > -1 is not supported for system-wide non-root.\n"
                      "To use it\n"
                      " * try --system-wide=no,\n"
                      " * run gatord as root,\n"
                      " * or make sure '/proc/sys/kernel/perf_event_paranoid' is set to -1.\n"
                      "   Try (as root):\n"
                      "    - echo -1 > /proc/sys/kernel/perf_event_paranoid");
        }
        else {
            if (isRoot) {
                LOG_SETUP("%s does not exist\nIs CONFIG_TRACING and CONFIG_CONTEXT_SWITCH_TRACER enabled?",
                          tracefsEventsPath);
                LOG_ERROR("%s is not available.\n"
                          "Try:\n"
                          " - mount -t debugfs none /sys/kernel/debug",
                          tracefsEventsPath);
            }
            else {
                LOG_SETUP("%s does not exist\nIs CONFIG_TRACING and CONFIG_CONTEXT_SWITCH_TRACER enabled?",
                          tracefsEventsPath);
                LOG_ERROR("%s is not available.\n"
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

    // prefer ftrace on android, or when perf cannot read the raw values
    const bool use_ftrace_for_cpu_frequency = (!can_access_raw_tracepoints) || is_android;

    // create the configuration object, from this point on perf is supported
    std::unique_ptr<PerfDriverConfiguration> configuration {new PerfDriverConfiguration()};

    configuration->config.has_fd_cloexec = (kernelVersion >= KERNEL_VERSION(3U, 14U, 0U));
    configuration->config.has_count_sw_dummy = (kernelVersion >= KERNEL_VERSION(3U, 12U, 0U));
    configuration->config.has_sample_identifier = (kernelVersion >= KERNEL_VERSION(3U, 12U, 0U));
    configuration->config.has_attr_comm_exec = (kernelVersion >= KERNEL_VERSION(3U, 16U, 0U));
    configuration->config.has_attr_mmap2 = (kernelVersion >= KERNEL_VERSION(3U, 16U, 0U));
    configuration->config.has_attr_clockid_support = (kernelVersion >= KERNEL_VERSION(4U, 1U, 0U));
    configuration->config.has_attr_context_switch = (kernelVersion >= KERNEL_VERSION(4U, 3U, 0U));
    configuration->config.has_ioctl_read_id = (kernelVersion >= KERNEL_VERSION(3U, 12U, 0U));
    configuration->config.has_aux_support = (kernelVersion >= KERNEL_VERSION(4U, 1U, 0U));
    configuration->config.has_exclude_callchain_kernel = (kernelVersion >= KERNEL_VERSION(3U, 7U, 0U));

    configuration->config.is_system_wide = systemWide;
    configuration->config.exclude_kernel = !can_collect_kernel_data;
    configuration->config.can_access_tracepoints = can_access_raw_tracepoints;

    configuration->config.has_armv7_pmu_driver = hasArmv7PmuDriver;

    configuration->config.has_64bit_uname = has_64bit_uname;
    configuration->config.use_64bit_register_set = use_64bit_register_set;

    configuration->config.use_ftrace_for_cpu_frequency = use_ftrace_for_cpu_frequency;

    // detect the PMUs
    std::set<const GatorCpu *> cpusDetectedViaSysFs;
    std::set<const GatorCpu *> cpusDetectedViaCpuid;
    bool haveFoundKnownCpuWithSpe = false;

    // Add supported PMUs
    FsEntry dir = FsEntry::create(PERF_DEVICES);
    if (dir.exists()) {
        auto children = dir.children();
        std::optional<FsEntry> dirent;
        while ((dirent = children.next())) {
            const std::string nameString = dirent->name();
            const char * const name = nameString.c_str();
            LOG_DEBUG("perf pmu: %s", name);
            const GatorCpu * gatorCpu = pmuXml.findCpuByName(name);
            if (gatorCpu != nullptr) {
                int type;
                const std::string path(lib::Format() << PERF_DEVICES << "/" << name << "/type");
                if (lib::readIntFromFile(path.c_str(), type) == 0) {
                    LOG_DEBUG("    ... using pmu type %d for %s cores", type, gatorCpu->getCoreName());
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
                    LOG_DEBUG("    ... is uncore pmu %s", uncorePmu->getCoreName());
                    configuration->uncores.push_back(PerfUncore {*uncorePmu, type});
                    continue;
                }
            }

            if (gator::smmuv3::detect_smmuv3_pmus(pmuXml, smmu_identifiers, *configuration, name)) {
                LOG_DEBUG("    ... is SMMUv3 PMU");
                continue;
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
                            LOG_DEBUG("    ... cpu %d, with cpuid 0x%05x", cpuNumber, cpuIdForCpu);
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
                                LOG_DEBUG("    ... using generic pmu type %d for %s cores",
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
                        LOG_DEBUG("    ... using SPE pmu type %d for cpu %d", type, cpuNumber);
                        configuration->cpuNumberToSpeType[cpuNumber] = type;
                    }
                    continue;
                }
            }
        }
    }
    else {
        LOG_DEBUG(PERF_DEVICES " doesn't exist");
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
            LOG_DEBUG("generic pmu: %s", gatorCpu->getCoreName());
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
        LOG_SETUP(
            "No Perf PMUs detected\n"
            "Could not detect any Perf PMUs in /sys/bus/event_source/devices/ but the system contains recognised CPUs. "
            "The system may not support perf hardware counters. Check CONFIG_HW_PERF_EVENTS is set and that the PMU is "
            "configured in the target device tree.");
    }

    return configuration;
}
