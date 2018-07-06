/**
 * Copyright (C) Arm Limited 2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PERFCONFIG_H
#define PERFCONFIG_H

struct PerfConfig
{
    bool has_fd_cloexec; // >=3.14
    bool has_count_sw_dummy; // >=3.12
    bool has_sample_identifier; // >= 3.12
    bool has_attr_comm_exec; // >= 3.16
    bool has_attr_mmap2; // >=3.16
    bool has_attr_clockid_support; // >= 4.1
    bool has_attr_context_switch; // >= 4.3
    bool has_ioctl_read_id; // >= 3.12
    bool has_aux_support; // >= 4.1

    bool is_system_wide;
    bool exclude_kernel;
    bool allow_system_wide;

    // can access tracepoints
    bool can_access_tracepoints;

    bool has_armv7_pmu_driver;
};

#endif // PERFCONFIG_H
