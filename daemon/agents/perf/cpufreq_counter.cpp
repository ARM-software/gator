/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#include "agents/perf/cpufreq_counter.h"

#include "lib/Span.h"
#include "lib/String.h"
#include "lib/Utils.h"

namespace agents::perf {

    std::optional<apc::perf_counter_t> read_cpu_frequency(
        int cpu_no,
        ICpuInfo const & cpu_info,
        lib::Span<perf_capture_configuration_t::cpu_freq_properties_t> cluster_keys_for_cpu_frequency_counter)
    {
        static constexpr std::size_t buffer_size = 128;
        static constexpr std::int64_t freq_multiplier = 1000;

        auto cluster_ids = cpu_info.getClusterIds();

        if ((cpu_no < 0) || (std::size_t(cpu_no) >= cluster_ids.size())) {
            return {};
        }

        auto cluster_id = cluster_ids[cpu_no];
        auto const & cpu_freq_keys = cluster_keys_for_cpu_frequency_counter;

        if ((cluster_id < 0) && (std::size_t(cluster_id) >= cpu_freq_keys.size())) {
            return {};
        }

        auto const & cpu_freq_key = cpu_freq_keys[cluster_id];

        if (cpu_freq_key.key < first_free_key) {
            return {};
        }

        char const * const pattern =
            (cpu_freq_key.use_cpuinfo ? "/sys/devices/system/cpu/cpu%i/cpufreq/cpuinfo_cur_freq"
                                      : "/sys/devices/system/cpu/cpu%i/cpufreq/scaling_cur_freq");

        lib::printf_str_t<buffer_size> buffer {pattern, cpu_no};
        std::int64_t freq = 0;
        if (lib::readInt64FromFile(buffer, freq) != 0) {
            freq = 0;
        }

        return apc::perf_counter_t {
            .core = cpu_no,
            .key = cpu_freq_key.key,
            .value = freq * freq_multiplier,
        };
    }
}
