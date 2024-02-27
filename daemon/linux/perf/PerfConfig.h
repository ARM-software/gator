/* Copyright (C) 2018-2024 by Arm Limited. All rights reserved. */

#ifndef PERFCONFIG_H
#define PERFCONFIG_H

struct PerfConfig {
    bool has_fd_cloexec = false;               // >=3.14
    bool has_count_sw_dummy = false;           // >=3.12
    bool has_sample_identifier = false;        // >= 3.12
    bool has_attr_comm_exec = false;           // >= 3.16
    bool has_attr_mmap2 = false;               // >=3.16
    bool has_attr_clockid_support = false;     // >= 4.1
    bool has_attr_context_switch = false;      // >= 4.3
    bool has_ioctl_read_id = false;            // >= 3.12
    bool has_aux_support = false;              // >= 4.1
    bool has_exclude_callchain_kernel = false; // >= 3.7
    bool has_perf_format_lost = false;         // >= 6.0
    bool supports_strobing = false;            // requires additional patches

    bool exclude_kernel = false;

    // can access tracepoints
    bool can_access_tracepoints = false;

    bool has_armv7_pmu_driver = false;

    // which register sets are available
    bool has_64bit_uname = false;
    bool use_64bit_register_set = false;

    // who is responsible for the cpu_frequency tracepoint event
    bool use_ftrace_for_cpu_frequency = false;
};

#endif // PERFCONFIG_H
