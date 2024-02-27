/* Copyright (C) 2022-2024 by Arm Limited. All rights reserved. */

#include "agents/perf/perf_driver_summary.h"

#include "Logging.h"
#include "Time.h"
#include "lib/String.h"
#include "linux/SysfsSummaryInformation.h"
#include "linux/perf/PerfConfig.h"

#include <cstdint>
#include <ctime>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include <sys/utsname.h>
#include <unistd.h>

namespace agents::perf {

    std::optional<perf_driver_summary_state_t> create_perf_driver_summary_state(PerfConfig const & perf_config,
                                                                                std::uint64_t monotonic_start,
                                                                                bool system_wide)
    {
        struct utsname utsname;
        if (uname(&utsname) != 0) {
            LOG_WARNING("uname() failed");
            return {};
        }

        lib::dyn_printf_str_t buf {"%s %s %s %s %s GNU/Linux",
                                   utsname.sysname,
                                   utsname.nodename,
                                   utsname.release,
                                   utsname.version,
                                   utsname.machine};

        long page_size = sysconf(_SC_PAGESIZE);
        if (page_size < 0) {
            LOG_WARNING("sysconf _SC_PAGESIZE failed");
            return {};
        }

        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
            LOG_WARNING("clock_gettime failed");
            return {};
        }

        const std::uint64_t clock_realtime = (ts.tv_sec * NS_PER_S) + ts.tv_nsec;

        if (clock_gettime(CLOCK_BOOTTIME, &ts) != 0) {
            LOG_WARNING("clock_gettime failed");
            return {};
        }

        const std::uint64_t clock_boottime = (ts.tv_sec * NS_PER_S) + ts.tv_nsec;

        std::map<std::string, std::string> additional_attributes {};

        additional_attributes["perf.is_root"] = (geteuid() == 0 ? "1" : "0");
        additional_attributes["perf.is_system_wide"] = (system_wide ? "1" : "0");
        additional_attributes["perf.can_access_tracepoints"] = (perf_config.can_access_tracepoints ? "1" : "0");
        additional_attributes["perf.has_attr_context_switch"] = (perf_config.has_attr_context_switch ? "1" : "0");

        lnx::addDefaultSysfsSummaryInformation(additional_attributes);

        return perf_driver_summary_state_t {
            std::move(additional_attributes),
            std::move(buf),
            clock_realtime,
            clock_boottime,
            monotonic_start,
            page_size,
            perf_config.has_attr_clockid_support,
        };
    }
}
