/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "ICpuInfo.h"
#include "agents/perf/capture_configuration.h"
#include "apc/perf_counter.h"
#include "lib/Span.h"

namespace agents::perf {
    /**
     * Attempt to read the current cpu frequency for some CPU frequency counter
     *
     * @param cpu_no The core no of the cpu to read
     * @param cpu_info The cpu info object for mapping cpu to cluster
     * @param cluster_keys_for_cpu_frequency_counter The lookup of cluster to cpu_freq counter properties
     * @return The counter value, or an empty optional if no counter exists
     */
    std::optional<apc::perf_counter_t> read_cpu_frequency(
        int cpu_no,
        ICpuInfo const & cpu_info,
        lib::Span<perf_capture_configuration_t::cpu_freq_properties_t> cluster_keys_for_cpu_frequency_counter);
}
