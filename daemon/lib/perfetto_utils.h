/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "capture/Environment.h"
#include "lib/WaitForProcessPoller.h"

namespace lib {
    [[nodiscard]] inline bool check_traced_running()
    {
        //Scan for traced process running.
        std::set<pid_t> traced_pids {};
        WaitForProcessPoller traced_poller("traced");

        return traced_poller.poll(traced_pids);
    }

    [[nodiscard]] inline bool is_android()
    {
        return capture::detectOs() == capture::OsType::Android;
    }

}
