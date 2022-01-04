/* Copyright (C) 2018-2021 by Arm Limited. All rights reserved. */

#ifndef PERFCONFIG_H
#define PERFCONFIG_H

struct PerfConfig {
    bool has_fd_cloexec = false;           // >=3.14
    bool has_count_sw_dummy = false;       // >=3.12
    bool has_sample_identifier = false;    // >= 3.12
    bool has_attr_comm_exec = false;       // >= 3.16
    bool has_attr_mmap2 = false;           // >=3.16
    bool has_attr_clockid_support = false; // >= 4.1
    bool has_attr_context_switch = false;  // >= 4.3
    bool has_ioctl_read_id = false;        // >= 3.12
    bool has_aux_support = false;          // >= 4.1

    bool is_system_wide = false;
    bool exclude_kernel = false;

    // can access tracepoints
    bool can_access_tracepoints = false;

    bool has_armv7_pmu_driver = false;

    // which register sets are available
    bool has_64bit_uname = false;
    bool use_64bit_register_set = false;
};

#endif // PERFCONFIG_H
