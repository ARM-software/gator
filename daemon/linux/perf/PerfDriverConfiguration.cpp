/* Copyright (C) 2013-2025 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfDriverConfiguration.h"

#include "Config.h"
#include "Configuration.h"
#include "Logging.h"
#include "SessionData.h"
#include "capture/Environment.h"
#include "k/perf_event.h"
#include "lib/AutoClosingFd.h"
#include "lib/CpuIdSet.h"
#include "lib/Error.h"
#include "lib/FileDescriptor.h"
#include "lib/Format.h"
#include "lib/FsEntry.h"
#include "lib/Popen.h"
#include "lib/Span.h"
#include "lib/Syscall.h"
#include "lib/Utils.h"
#include "lib/midr.h"
#include "linux/CoreOnliner.h"
#include "linux/smmu_identifier.h"
#include "linux/smmu_support.h"
#include "xml/PmuXML.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#include <sched.h>
#undef _GNU_SOURCE
#else
#include <sched.h>
#endif

#include "setup_warnings.h"

#include <sstream>

#include <sys/utsname.h>
#include <unistd.h>

#define PERF_DEVICES "/sys/bus/event_source/devices"

using namespace gator::smmuv3;
using lib::FsEntry;

namespace {
    constexpr std::string_view securityPerfHardenPropString = "security.perf_harden";

    [[nodiscard]] bool getPerfHarden()
    {
        const char * const command[] = {"getprop", securityPerfHardenPropString.data(), nullptr};
        const lib::PopenResult getprop = lib::popen(command);
        if (getprop.pid < 0) {
            LOG_DEBUG("lib::popen(%s %s) failed: %s. Probably not android",
                      command[0],
                      command[1],
                      lib::strerror(-getprop.pid));
            return false;
        }

        char value = '0';
        lib::readAll(getprop.out, &value, 1);
        lib::pclose(getprop);
        return value == '1';
    }

    void setProp(std::string_view prop, const std::string & value)
    {
        const char * const command[] = {"setprop", prop.data(), value.c_str(), nullptr};

        const lib::PopenResult setPropResult = lib::popen(command);
        //setprop not found, probably not Android.
        if (setPropResult.pid == -ENOENT) {
            LOG_DEBUG("lib::popen(%s %s %s) failed: %s",
                      command[0],
                      command[1],
                      command[2],
                      lib::strerror(-setPropResult.pid));
            return;
        }
        if (setPropResult.pid < 0) {
            LOG_ERROR("lib::popen(%s %s %s) failed: %s",
                      command[0],
                      command[1],
                      command[2],
                      lib::strerror(-setPropResult.pid));
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

    void setPerfHarden(bool on)
    {
        setProp(securityPerfHardenPropString, on ? "1" : "0");
    }

    /**
     * @return true if perf harden in now off
     */
    [[nodiscard]] bool disablePerfHarden()
    {
        if (!getPerfHarden()) {
            return true;
        }

        LOG_WARNING("disabling property %s", securityPerfHardenPropString.data());

        setPerfHarden(false);

        sleep(1);

        return !getPerfHarden();
    }

    [[nodiscard]] bool beginsWith(const char * string, const char * prefix)
    {
        return strncmp(string, prefix, strlen(prefix)) == 0;
    }

    [[nodiscard]] bool pmu_supports_strobing_patches(lib::FsEntry const & entry)
    {
        auto const format_path = lib::FsEntry::create(entry, "format");
        auto const strobe_period_path = lib::FsEntry::create(format_path, "strobe_period");

        if (strobe_period_path.exists()) {
            LOG_DEBUG("Target supports event strobing.");
            return true;
        }

        return false;
    }

    [[nodiscard]] GatorCpu with_max_counters_value(
        GatorCpu const & gatorCpu,
        std::unordered_map<cpu_utils::cpuid_t, std::size_t> const & max_event_count_by_cpuid)
    {
        std::size_t value {};
        bool all_invalid = false;
        bool any_values = false;

        // Don't override the user; assume they know best
        if (gSessionData.mOverrideNoPmuSlots > 0) {
            return gatorCpu;
        }

        for (auto cpuId : gatorCpu.getCpuIds()) {
            if (auto const it = max_event_count_by_cpuid.find(cpuId); it != max_event_count_by_cpuid.end()) {
                // the number includes the cycle counter, so the number of programmable is one less
                auto const avail = (it->second > 1 ? it->second - 1 : 0);

                if (any_values) {
                    // assume any zero values are errors, but otherwise find the smallest non-zero value
                    if (avail > 0) {
                        if (value == 0) {
                            value = avail;
                        }
                        else {
                            value = std::min(value, avail);
                        }
                    }

                    // if not even the cycle counter is open then assume some other error
                    all_invalid &= (it->second == 0);
                }
                else {
                    any_values = true;
                    value = avail;

                    // if not even the cycle counter is open then assume some other error
                    all_invalid = (it->second == 0);
                }
            }
        }

        if (!any_values) {
            LOG_SETUP("CPU Counters\nCould not determine the number of supported programmable events for %s, using the "
                      "provided default: %d",
                      gatorCpu.getCoreName(),
                      gatorCpu.getPmncCounters());
            return gatorCpu;
        }

        if (all_invalid) {
            LOG_SETUP("CPU Counters\nCould not determine the number of supported programmable events for %s, probing "
                      "indicates that there are no counters available, using the provided default: %d",
                      gatorCpu.getCoreName(),
                      gatorCpu.getPmncCounters());
            return gatorCpu;
        }

        if (static_cast<int>(value) < gatorCpu.getPmncCounters()) {
            LOG_WARNING("Detected %zu programmable event counters for %s PMU (expected %d)",
                        value,
                        gatorCpu.getCoreName(),
                        gatorCpu.getPmncCounters());
        }

        LOG_SETUP("CPU Counters\nDetected %zu programmable events for %s", value, gatorCpu.getCoreName());
        return gatorCpu.withUpdatedPmncCount(static_cast<int>(value));
    }

    template<std::size_t max_possible_events>
    [[nodiscard]] std::size_t count_valid_readable_counters(int fd)
    {
        std::array<std::uint64_t, (2 * max_possible_events) + 1> buffer {};

        auto const res = lib::read(fd, buffer.data(), buffer.size() * sizeof(std::uint64_t));
        auto const n_ints = res / sizeof(std::uint64_t);

        std::size_t result = 0;

        if (n_ints > 0) {
            LOG_DEBUG("Successfully read %zd bytes of data from counters.", res);
            auto const count = buffer[0];
            auto const avail = (n_ints - 1) / 2;
            LOG_DEBUG("...count=%" PRIu64 ", avail=%zi", count, avail);
            auto const limit = std::min<std::size_t>(count, avail);

            for (std::size_t i = 0; i < limit; ++i) {
                auto const va = buffer[i * 2 + 1];
                auto const id = buffer[i * 2 + 2];

                LOG_DEBUG("...[%zu] = %" PRIu64 ": %" PRIu64, i, id, va);

                // all counters should give a non-zero value, assume some issue if it is
                // (this happens on android, for example, when the performance governer steals some counters)
                if (va != 0) {
                    result += 1;
                }
            }
        }
        else {
            LOG_DEBUG("Failed to read data from counters due to res=%zd, errno=%d (%s)", res, errno, lib::strerror());
        }

        return result;
    }

    [[nodiscard]] std::size_t calculate_max_event_count_for(std::size_t cpuNo)
    {
        constexpr std::size_t max_possible_events = 32;
        constexpr unsigned affine_loop_count = 5;

        std::vector<lib::AutoClosingFd> event_fds {};

        LOG_DEBUG("Determining max events for #%zu", cpuNo);

        lib::CpuIdSet cpuset(cpuNo + 1);
        cpuset.add(cpuNo);

        // try and set affinity to ensure cpuNo is doing some work (and won't read 0 events)
        // Note: This needs to be restored elsewhere.
        bool affinitySucceeded = false;
        for (unsigned count = 0; count < affine_loop_count && !affinitySucceeded; ++count) {
            if (lib::sched_setaffinity(0, cpuset) == 0) {
                affinitySucceeded = true;
            }
        }

        if (!affinitySucceeded) {
            LOG_WARNING("Error calling sched_setaffinity on %zu: %d (%s)", cpuNo, errno, lib::strerror());
        }

        // make sure we are definitely running on the CPU...
        sched_yield();

        // count the supported counters
        std::size_t actual_count = 0;
        for (std::size_t n = 0; n < max_possible_events; ++n) {
            perf_event_attr attr {};

            attr.type = PERF_TYPE_HARDWARE;
            attr.size = sizeof(perf_event_attr);
            attr.config = (n == 0 ? PERF_COUNT_HW_CPU_CYCLES : PERF_COUNT_HW_INSTRUCTIONS);
            attr.sample_period = 1000000 + n; //NOLINT(readability-magic-numbers)
            //NOLINTNEXTLINE(hicpp-signed-bitwise)
            attr.sample_type = PERF_SAMPLE_READ | PERF_SAMPLE_IDENTIFIER | PERF_SAMPLE_TIME;
            attr.read_format = PERF_FORMAT_ID | PERF_FORMAT_GROUP;
            attr.disabled = 0;
            attr.pinned = (n == 0);
            attr.exclude_kernel = 1;

            lib::AutoClosingFd fd {
                lib::perf_event_open(&attr, 0, cpuNo, (n == 0 ? -1 : event_fds[0].get()), 0),
            };

            if (fd.get() >= 0) {
                auto const n_valid = count_valid_readable_counters<max_possible_events>(fd.get());

                if (n_valid > event_fds.size()) {
                    event_fds.emplace_back(std::move(fd));
                    actual_count = n_valid;
                }
                else {
                    break;
                }
            }
            else {
                LOG_DEBUG("Failed to open test event# %zu due to %d (%s)", n, errno, lib::strerror());
                break;
            }
        }

        LOG_DEBUG("Finished loop for #%zu", cpuNo);

        return actual_count;
    }

    [[nodiscard]] std::unordered_map<cpu_utils::cpuid_t, std::size_t> calculate_max_event_count_by_cpuid(
        lib::Span<cpu_utils::midr_t const> midrs)
    {
        constexpr unsigned affine_loop_count = 5;

        std::unordered_map<cpu_utils::cpuid_t, std::size_t> max_event_count_by_cpuid {};

        if (gSessionData.mOverrideNoPmuSlots > 0) {
            return max_event_count_by_cpuid;
        }

        // record the current thread affinity so it can be restored later
        lib::CpuIdSet original_cpuset(midrs.size());
        if (lib::sched_getaffinity(0, original_cpuset) < 0) {
            LOG_DEBUG("Error calling sched_getaffinity to get current mask: %d (%s)", errno, lib::strerror());

            original_cpuset.clear();

            for (std::size_t cpuNo = 0; cpuNo < midrs.size(); ++cpuNo) {
                original_cpuset.add(cpuNo);
            }
        }

        // for each CPU, determine how many events can be supported
        for (std::size_t cpuNo = 0; cpuNo < midrs.size(); ++cpuNo) {
            if (!CoreOnliner::isCoreOnline(cpuNo).value_or(true)) {
                LOG_DEBUG("Skipping max events detection for offline core %zu.", cpuNo);
                continue;
            }

            auto const actual_count = calculate_max_event_count_for(cpuNo);
            auto [it, inserted] = max_event_count_by_cpuid.try_emplace(midrs[cpuNo].to_cpuid(), actual_count);
            if (!inserted) {
                it->second = std::max(it->second, actual_count);
            }
        }

        for (auto const & [cpuId, count] : max_event_count_by_cpuid) {
            LOG_DEBUG("Determined CPUs with ID 0x%05x can support at most %zu events.", cpuId.to_raw_value(), count);
        }

        // restore affinity
        {

            // try and set affinity
            bool affinitySucceeded = false;
            for (unsigned count = 0; count < affine_loop_count && !affinitySucceeded; ++count) {
                if (lib::sched_setaffinity(0, original_cpuset) == 0) {
                    affinitySucceeded = true;
                }
            }

            if (!affinitySucceeded) {
                LOG_WARNING("Error calling sched_setaffinity to restore all: %d (%s)", errno, lib::strerror());
            }
        }

        return max_event_count_by_cpuid;
    }

    void create_perf_event_paranoid_error(setup_warnings_t & setup_warnings,
                                          int current_paraniod_value,
                                          bool wants_system_wide)
    {
        const auto needed_paranoid_value = wants_system_wide ? -1 : 2;
        std::stringstream str;
        str << "The perf security settings will prevent collection of profiling data. The value of "
               "/proc/sys/kernel/perf_event_paranoid is "
            << current_paraniod_value
            << ". Run the following command on the target to enable data collection: "
               " `echo "
            << needed_paranoid_value << " > /proc/sys/kernel/perf_event_paranoid`";
        setup_warnings.add_error(str.str());
    }
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
std::unique_ptr<PerfDriverConfiguration> PerfDriverConfiguration::detect(CaptureOperationMode captureOperationMode,
                                                                         const char * tracefsEventsPath,
                                                                         lib::Span<const cpu_utils::midr_t> midrs,
                                                                         const default_identifiers_t & smmu_identifiers,
                                                                         const PmuXML & pmuXml,
                                                                         setup_warnings_t & setup_warnings)
{
    struct utsname utsname;
    if (lib::uname(&utsname) != 0) {
        LOG_ERROR("uname failed");
        return nullptr;
    }

    LOG_DEBUG("Kernel version: %s", utsname.release);

    auto const systemWide = isCaptureOperationModeSystemWide(captureOperationMode);

    // Check the kernel version
    auto const kernelVersion = lib::parseLinuxVersion(utsname);
    setup_warnings.kernel_version = kernelVersion;

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
        static const char * error_message = "Unsupported kernel version\nPlease upgrade to 3.4 or later";
        LOG_SETUP("%s", error_message);
        LOG_ERROR("%s", error_message);
        setup_warnings.add_error("The target's kernel is too old. Please upgrade to kernel version 3.4 or later.");
        return nullptr;
    }

    const auto os_type = capture::detectOs();
    setup_warnings.os_type = os_type;
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
        setup_warnings.add_error("The Android security settings will prevent collection of perf data. "
                                 "Please run the following command on the device as the shell or root user: "
                                 "`setprop security.perf_harden 0`");
        return nullptr;
    }

    int perf_event_paranoid;
    if (lib::readIntFromFile("/proc/sys/kernel/perf_event_paranoid", perf_event_paranoid) != 0) {
        if (isRoot) {
            LOG_SETUP("perf_event_paranoid not accessible\n"
                      "Is CONFIG_PERF_EVENTS enabled?");
            LOG_ERROR("perf_event_paranoid not accessible\n"
                      "Is CONFIG_PERF_EVENTS enabled?");
            setup_warnings.add_error("The /proc/sys/kernel/perf_event_paranoid file could not be read. "
                                     "Please check that perf support is enabled for this kernel.");
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

    bool has_errors = false;

    if (!can_collect_any_data) {
        // This is only actually true if the kernel has the grsecurity PERF_HARDEN patch
        // but we assume no-one would ever set perf_event_paranoid > 2 without it.
        LOG_SETUP("perf_event_open\nperf_event_paranoid > 2 is not supported for non-root");
        LOG_ERROR("perf_event_open: perf_event_paranoid > 2 is not supported for non-root.\n"
                  "To use it try (as root):\n"
                  "  echo 2 > /proc/sys/kernel/perf_event_paranoid");

        create_perf_event_paranoid_error(setup_warnings, perf_event_paranoid, systemWide);
        has_errors = true;
    }
    else if (systemWide && !can_collect_system_wide_data) {
        LOG_SETUP("System wide tracing\nperf_event_paranoid > 0 is not supported for system-wide non-root");
        LOG_ERROR("perf_event_open: perf_event_paranoid > 0 is not supported for system-wide non-root.\n"
                  "To use it\n"
                  " * try --system-wide=no,\n"
                  " * run gatord as root,\n"
                  " * or make sure '/proc/sys/kernel/perf_event_paranoid' is set to -1.\n"
                  "   Try (as root):\n"
                  "    - echo -1 > /proc/sys/kernel/perf_event_paranoid");
        create_perf_event_paranoid_error(setup_warnings, perf_event_paranoid, systemWide);
        has_errors = true;
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

            create_perf_event_paranoid_error(setup_warnings, perf_event_paranoid, systemWide);
            has_errors = true;
        }
        else {
            if (isRoot) {
                LOG_SETUP("%s does not exist\nIs CONFIG_TRACING and CONFIG_CONTEXT_SWITCH_TRACER enabled?",
                          tracefsEventsPath);
                LOG_ERROR("%s is not available.\n"
                          "Try:\n"
                          " - mount -t debugfs none /sys/kernel/debug",
                          tracefsEventsPath);
                std::stringstream msg("Profiling in system-wide mode requires access to '");
                msg << tracefsEventsPath
                    << "' but this is not accessible. Check that debugfs is mounted. "
                       "You can run the following command on the target to mount it: 'mount -t debugfs none "
                       "/sys/kernel/debug'";
                setup_warnings.add_warning(msg.str());
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
                std::stringstream msg("Profiling in system-wide mode requires access to '");
                msg << tracefsEventsPath
                    << "' but this is not accessible with the current user. "
                       "Check that debugfs is mounted with the correct read/write permissions. "
                       "You can run the following commands on the target to remount it: "
                       "'mount -o remount,mode=755 /sys/kernel/debug' and 'mount -o remount,mode=755 "
                       "/sys/kernel/debug/tracing'";
                setup_warnings.add_warning(msg.str());
            }
        }
        has_errors = true;
    }

    // everything after here requires perf so, if we've already got errors, there's no point continuing.
    if (has_errors) {
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
    configuration->config.has_attr_build_id = (kernelVersion >= KERNEL_VERSION(5U, 12U, 0U));
    configuration->config.has_attr_clockid_support = (kernelVersion >= KERNEL_VERSION(4U, 1U, 0U));
    configuration->config.has_attr_context_switch = (kernelVersion >= KERNEL_VERSION(4U, 3U, 0U));
    configuration->config.has_ioctl_read_id = (kernelVersion >= KERNEL_VERSION(3U, 12U, 0U));
    configuration->config.has_aux_support = (kernelVersion >= KERNEL_VERSION(4U, 1U, 0U));
    configuration->config.has_exclude_callchain_kernel = (kernelVersion >= KERNEL_VERSION(3U, 7U, 0U));
    configuration->config.has_perf_format_lost = (kernelVersion >= KERNEL_VERSION(6U, 0U, 0U));
    configuration->config.supports_strobing_patches = false;
    configuration->config.supports_strobing_core = false;
    configuration->config.supports_inherit_sample_read = false;

    configuration->config.exclude_kernel = !can_collect_kernel_data;
    configuration->config.can_access_tracepoints = can_access_raw_tracepoints;

    configuration->config.has_armv7_pmu_driver = hasArmv7PmuDriver;

    configuration->config.has_64bit_uname = has_64bit_uname;
    configuration->config.use_64bit_register_set = use_64bit_register_set;

    configuration->config.use_ftrace_for_cpu_frequency = use_ftrace_for_cpu_frequency;

    // detect supports_strobing_core
    {
        perf_event_attr attr {};

        attr.type = PERF_TYPE_SOFTWARE;
        attr.size = sizeof(perf_event_attr);
        attr.config = PERF_COUNT_SW_TASK_CLOCK;
        attr.sample_period = 1000000; // NOLINT(readability-magic-numbers)
        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        attr.sample_type = PERF_SAMPLE_READ | PERF_SAMPLE_IDENTIFIER | PERF_SAMPLE_CPU | PERF_SAMPLE_TIME;
        attr.read_format = PERF_FORMAT_ID;
        attr.disabled = 1;
        attr.inherit = 0;
        attr.exclude_kernel = 1;
        attr.alternative_sample_period = 10000; // NOLINT(readability-magic-numbers)

        auto const fd = lib::perf_event_open(&attr, 0, 0, -1, 0);

        if (fd >= 0) {
            LOG_DEBUG("Detected support for alternative sample period features");
            configuration->config.supports_strobing_core = true;
            setup_warnings.supports_counter_strobing = tri_bool_t::yes;

            close(fd);
        }
        else {
            auto const e = errno;
            //NOLINTNEXTLINE(concurrency-mt-unsafe)
            LOG_DEBUG("No support for alternative sample period features, error was %d (%s)", e, std::strerror(e));
            setup_warnings.supports_counter_strobing = tri_bool_t::no;
            setup_warnings.add_warning("The target does not support perf counter strobing. Metrics collection "
                                       "may result in high CPU usage. To resolve this warning, recompile your kernel "
                                       "with the counter strobing patch applied.");
        }
    }

    // detect supports_inherit_sample_read
    {
        perf_event_attr attr {};

        attr.type = PERF_TYPE_SOFTWARE;
        attr.size = sizeof(perf_event_attr);
        attr.config = PERF_COUNT_SW_TASK_CLOCK;
        attr.sample_period = 1000000; // NOLINT(readability-magic-numbers)
        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        attr.sample_type = PERF_SAMPLE_READ | PERF_SAMPLE_IDENTIFIER | PERF_SAMPLE_TIME | PERF_SAMPLE_TID;
        attr.read_format = PERF_FORMAT_ID | PERF_FORMAT_GROUP;
        attr.disabled = 1;
        attr.inherit = 1;
        attr.inherit_stat = 1; // the original kernel patches require this, the later ones do not (they ignore)
        attr.exclude_kernel = 1;

        auto const fd = lib::perf_event_open(&attr, 0, 0, -1, 0);

        if (fd >= 0) {
            LOG_DEBUG("Detected support for inheritable counter groups");
            configuration->config.supports_inherit_sample_read = true;
            setup_warnings.supports_event_inherit = tri_bool_t::yes;
            close(fd);
        }
        else {
            auto const e = errno;
            //NOLINTNEXTLINE(concurrency-mt-unsafe)
            LOG_DEBUG("No support for inheritable counter groups, error was %d (%s)", e, std::strerror(e));
            setup_warnings.supports_event_inherit = tri_bool_t::no;
            setup_warnings.add_warning("The target does not support inheritable counter groups. This can result "
                                       "in creation of large numbers of file descriptors which might cause the capture "
                                       "to fail.");
        }
    }

    // detect max number of events per PMU
    auto const max_event_count_by_cpuid = calculate_max_event_count_by_cpuid(midrs);
    setup_warnings.number_of_counters_by_cpu = max_event_count_by_cpuid;

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
                    configuration->cpus.emplace_back(with_max_counters_value(*gatorCpu, max_event_count_by_cpuid),
                                                     type);
                    cpusDetectedViaSysFs.insert(gatorCpu);
                    if (gatorCpu->getSpeName() != nullptr) {
                        haveFoundKnownCpuWithSpe = true;
                    }
                    continue;
                }

                configuration->config.supports_strobing_patches |= pmu_supports_strobing_patches(*dirent);
            }

            const UncorePmu * uncorePmu = pmuXml.findUncoreByName(name);
            if (uncorePmu != nullptr) {
                int type;
                const std::string path(lib::Format() << PERF_DEVICES << "/" << name << "/type");
                if (lib::readIntFromFile(path.c_str(), type) == 0) {
                    LOG_DEBUG("    ... is uncore pmu %s", uncorePmu->getCoreName());
                    if (!systemWide && beginsWith(name, "arm_cmn_")) {
                        LOG_SETUP("CMN detected but CMN events not enabled. CMN events are only available on "
                                  "system-wide captures.");
                    }
                    else {
                        configuration->uncores.emplace_back(*uncorePmu, type);
                    }
                    continue;
                }
            }

            if (gator::smmuv3::detect_smmuv3_pmus(pmuXml, smmu_identifiers, *configuration, name, systemWide)) {
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
                    configuration->config.supports_strobing_patches |= pmu_supports_strobing_patches(*dirent);

                    const auto cpuNumbers = lib::readCpuMaskFromFile(maskPath.c_str());
                    if (!cpuNumbers.empty()) {
                        cpu_utils::cpuid_t cpuIdForType;
                        for (const int cpuNumber : cpuNumbers) {
                            // track generic pmu's type to the cpuId associated with it.
                            // if multiple different cpuIds are associated, then use -1 as cannot map to a unique pmu.
                            const auto cpuIdForCpu = midrs[cpuNumber].to_cpuid();
                            LOG_DEBUG("    ... cpu %d, with cpuid 0x%05x", cpuNumber, cpuIdForCpu.to_raw_value());
                            if (cpuIdForCpu.invalid_or_other()) {
                                // skip it as we don't know what it is. fair to assume
                                // homogeneous clusters.
                                continue;
                            }
                            if (!cpuIdForType.valid()) {
                                cpuIdForType = cpuIdForCpu;
                            }
                            else if (cpuIdForType != cpuIdForCpu) {
                                cpuIdForType = cpu_utils::cpuid_t::other;
                            }
                        }
                        if (!cpuIdForType.invalid_or_other()) {
                            const GatorCpu * gatorCpu = pmuXml.findCpuById(cpuIdForType);
                            if (gatorCpu != nullptr) {
                                LOG_DEBUG("    ... using generic pmu type %d for %s cores",
                                          type,
                                          gatorCpu->getCoreName());
                                configuration->cpus.emplace_back(
                                    with_max_counters_value(*gatorCpu, max_event_count_by_cpuid),
                                    type);
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
                    const auto cpuNumbers = lib::readCpuMaskFromFile(maskPath.c_str());
                    for (const int cpuNumber : cpuNumbers) {
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
    std::set<cpu_utils::cpuid_t> unrecognisedCpuIds;
    for (auto const & midr : midrs) {
        auto const cpuId = midr.to_cpuid();
        const GatorCpu * gatorCpu = pmuXml.findCpuById(cpuId);
        if (gatorCpu == nullptr) {
            // track the unknown cpuid (filter out some junk values)
            if (!cpuId.invalid_or_other()) {
                unrecognisedCpuIds.insert(cpuId);
            }
        }
        else if ((cpusDetectedViaSysFs.count(gatorCpu) == 0) && (cpusDetectedViaCpuid.count(gatorCpu) == 0)) {
            LOG_DEBUG("generic pmu: %s", gatorCpu->getCoreName());
            configuration->cpus.emplace_back(with_max_counters_value(*gatorCpu, max_event_count_by_cpuid),
                                             PERF_TYPE_RAW);
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
            cpu = PerfCpu {
                with_max_counters_value(GatorCpu(currentValue.gator_cpu, ARMV82_SPE, "v1p1"), max_event_count_by_cpuid),
                currentValue.pmu_type,
            };
        }
    }

    // insert 'Other' for unknown CPUIDs / no cpus found
    if ((hasNoCpus || addOtherForUnknownSpe) && unrecognisedCpuIds.empty()) {
        unrecognisedCpuIds.insert(cpu_utils::cpuid_t::other);
    }

    if (!unrecognisedCpuIds.empty()) {
        logCpuNotFound();
        const char * const speName = (addOtherForUnknownSpe ? ARMV82_SPE : nullptr);
        const char * const speVersion = (addOtherForUnknownSpe ? "v1p1" : nullptr);

        const int defaultPmncCounters = 6;

#if defined(__aarch64__)
        configuration->cpus.emplace_back(with_max_counters_value(
                                             GatorCpu {
                                                 "Other",
                                                 "Other",
                                                 "Other",
                                                 nullptr,
                                                 speName,
                                                 speVersion,
                                                 unrecognisedCpuIds,
                                                 defaultPmncCounters,
                                                 true,
                                             },
                                             max_event_count_by_cpuid),
                                         PERF_TYPE_RAW);
#elif defined(__arm__)
        configuration->cpus.emplace_back(with_max_counters_value(
                                             GatorCpu {
                                                 "Other",
                                                 "Other",
                                                 "Other",
                                                 nullptr,
                                                 speName,
                                                 speVersion,
                                                 unrecognisedCpuIds,
                                                 defaultPmncCounters,
                                                 anyV8,
                                             },
                                             max_event_count_by_cpuid),
                                         PERF_TYPE_RAW);
#else
        configuration->cpus.emplace_back(with_max_counters_value(
                                             GatorCpu {
                                                 "Other",
                                                 "Perf_Hardware",
                                                 "Perf_Hardware",
                                                 nullptr,
                                                 speName,
                                                 speVersion,
                                                 unrecognisedCpuIds,
                                                 defaultPmncCounters,
                                                 false,
                                             },
                                             max_event_count_by_cpuid),
                                         PERF_TYPE_HARDWARE);
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
