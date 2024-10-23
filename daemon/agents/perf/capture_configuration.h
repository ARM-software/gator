/* Copyright (C) 2022-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "Configuration.h"
#include "ICpuInfo.h"
#include "SessionData.h"
#include "agents/perf/events/event_configuration.hpp"
#include "agents/perf/events/types.hpp"
#include "agents/perf/record_types.h"
#include "ipc/messages.h"
#include "lib/midr.h"
#include "linux/perf/PerfConfig.h"
#include "linux/perf/PerfEventGroup.h"
#include "linux/perf/PerfEventGroupIdentifier.h"
#include "linux/perf/PerfGroups.h"
#include "xml/PmuXML.h"

namespace agents::perf {

    /** Validated and extracted fields from the received message */
    struct perf_capture_configuration_t {
        using perf_config_t = PerfConfig;
        using gator_cpu_t = GatorCpu;
        using uncore_pmu_t = UncorePmu;
        using capture_operation_mode_t = CaptureOperationMode;

        struct session_data_t {
            std::uint64_t live_rate;
            std::int32_t total_buffer_size;
            std::int32_t sample_rate;
            capture_operation_mode_t capture_operation_mode;
            bool one_shot;
            bool exclude_kernel_events;
            bool stop_on_exit;
        };

        struct command_t {
            std::string command;
            std::vector<std::string> args;
            std::string cwd;
            uid_t uid;
            gid_t gid;
        };

        struct cpu_freq_properties_t {
            std::int32_t key;
            bool use_cpuinfo;
        };

        session_data_t session_data {};
        perf_config_t perf_config {};
        std::vector<gator_cpu_t> clusters {};
        std::vector<cpu_freq_properties_t> cluster_keys_for_cpu_frequency_counter {};
        std::vector<std::int32_t> per_core_cluster_index {};
        std::vector<cpu_utils::midr_t> per_core_midrs {};
        std::map<core_no_t, std::uint32_t> per_core_spe_type {};
        std::vector<uncore_pmu_t> uncore_pmus {};
        std::map<cpu_utils::cpuid_t, std::string> cpuid_to_core_name {};
        std::map<std::uint32_t, std::string> perf_pmu_type_to_name {};
        event_configuration_t event_configuration {};
        buffer_config_t ringbuffer_config {};
        std::optional<command_t> command {};
        std::string wait_process {};
        std::string android_pkg {};
        std::set<pid_t> pids {};
        std::uint32_t num_cpu_cores {};
        bool enable_on_exec {};
        bool stop_pids {};
    };

    /**
     * Create a capture configuration msg object from various bits of state
     */
    [[nodiscard]] ipc::msg_capture_configuration_t create_capture_configuration_msg(
        SessionData const & session_data,
        PerfConfig const & perf_config,
        ICpuInfo const & cpu_info,
        std::map<int, std::uint32_t> const & cpu_number_to_spe_type,
        lib::Span<perf_capture_configuration_t::cpu_freq_properties_t> cluster_keys_for_cpu_frequency_counter,
        lib::Span<UncorePmu const> uncore_pmus,
        lib::Span<GatorCpu const> all_known_cpu_pmus,
        perf_groups_configurer_state_t const & perf_groups,
        agents::perf::buffer_config_t const & ringbuffer_config,
        std::map<std::uint32_t, std::string> const & perf_pmu_type_to_name,
        bool enable_on_exec,
        bool stop_pids);

    /** Add a command to execute (for --app, --allow-cmd) */
    void add_command(ipc::msg_capture_configuration_t & msg,
                     lib::Span<std::string const> cmd_args,
                     char const * working_dir,
                     uid_t uid,
                     gid_t gid);

    /** Add the name for --wait-process */
    void add_wait_for_process(ipc::msg_capture_configuration_t & msg, const char * command);

    /** Add the android package name  for --android-pkg */
    void add_android_package(ipc::msg_capture_configuration_t & msg, const char * android_pkg);

    /** Add the pids for --pids */
    void add_pids(ipc::msg_capture_configuration_t & msg, std::set<int> const & pids);

    /** Extract and validate the fields from the received msg. (Passed by value to allow moving out strings, rather than copying) */
    [[nodiscard]] std::shared_ptr<perf_capture_configuration_t> parse_capture_configuration_msg(
        ipc::msg_capture_configuration_t msg);
};
