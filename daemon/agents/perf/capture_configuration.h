/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "ICpuInfo.h"
#include "SessionData.h"
#include "ipc/messages.h"
#include "k/perf_event.h"
#include "lib/Assert.h"
#include "linux/perf/PerfBuffer.h"
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
        using perf_groups_t = perf_groups_common_serialized_state_t<perf_event_group_activator_state_t>;

        struct session_data_t {
            std::uint64_t live_rate;
            std::int32_t total_buffer_size;
            std::int32_t sample_rate;
            bool one_shot;
            bool exclude_kernel_events;
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
        std::vector<std::int32_t> per_core_cpuids {};
        std::map<std::int32_t, std::int32_t> per_core_spe_type {};
        std::vector<uncore_pmu_t> uncore_pmus {};
        std::map<std::uint32_t, std::string> cpuid_to_core_name {};
        perf_groups_t perf_groups {};
        perf_ringbuffer_config_t ringbuffer_config {};
        std::optional<command_t> command {};
        std::optional<std::string> wait_process {};
        std::set<pid_t> pids {};
        std::uint32_t num_cpu_cores {};
        bool enable_on_exec {};
    };

    /**
     * Create a capture configuration msg object from various bits of state
     */
    [[nodiscard]] ipc::msg_capture_configuration_t create_capture_configuration_msg(
        SessionData const & session_data,
        PerfConfig const & perf_config,
        ICpuInfo const & cpu_info,
        std::map<int, int> const & cpu_number_to_spe_type,
        lib::Span<perf_capture_configuration_t::cpu_freq_properties_t> cluster_keys_for_cpu_frequency_counter,
        lib::Span<UncorePmu const> uncore_pmus,
        lib::Span<GatorCpu const> all_known_cpu_pmus,
        perf_groups_configurer_state_t const & perf_groups,
        perf_ringbuffer_config_t const & ringbuffer_config,
        bool enable_on_exec);

    /** Add a command to execute (for --app, --allow-cmd) */
    void add_command(ipc::msg_capture_configuration_t & msg,
                     lib::Span<std::string const> cmd_args,
                     char const * working_dir,
                     uid_t uid,
                     gid_t gid);

    /** Add the name for --wait-process */
    void add_wait_for_process(ipc::msg_capture_configuration_t & msg, const char * command);

    /** Add the pids for --pids */
    void add_pids(ipc::msg_capture_configuration_t & msg, std::set<int> const & pids);

    /** Extract and validate the fields from the received msg. (Passed by value to allow moving out strings, rather than copying) */
    [[nodiscard]] std::unique_ptr<perf_capture_configuration_t> parse_capture_configuration_msg(
        ipc::msg_capture_configuration_t msg);
};
