/* Copyright (C) 2022-2023 by Arm Limited. All rights reserved. */

#include "agents/perf/events/perf_event_utils.hpp"

#include "agents/perf/events/types.hpp"
#include "k/perf_event.h"
#include "lib/Assert.h"
#include "lib/EnumUtils.h"
#include "lib/Format.h"

#include <cstddef>
#include <cstdint>
#include <ios>
#include <string>

namespace agents::perf {

    char const * perf_event_printer_t::map_core_cluster_name(core_no_t core_no)
    {
        auto index = lib::toEnumValue(core_no);
        runtime_assert(((index >= 0) && (std::size_t(index) < per_core_cpuids.size())), "Unexpected core no");
        auto cpuid = per_core_cpuids[index];
        auto it = cpuid_to_core_name.find(cpuid);
        if (it == cpuid_to_core_name.end()) {
            return "Unknown";
        }
        return it->second.c_str();
    }

    char const * perf_event_printer_t::map_custom_pmu_type(std::uint32_t type, core_no_t core_no)
    {
        // use provided label?
        auto it = perf_pmu_type_to_name.find(type);
        if (it != perf_pmu_type_to_name.end()) {
            return it->second.c_str();
        }

        // lookup core name
        return map_core_cluster_name(core_no);
    }

    char const * perf_event_printer_t::map_attr_type(std::uint32_t type, core_no_t core_no)
    {
        switch (type) {
            case PERF_TYPE_HARDWARE:
                return "cpu";
            case PERF_TYPE_BREAKPOINT:
                return "breakpoint";
            case PERF_TYPE_HW_CACHE:
                return "hw-cache";
            case PERF_TYPE_RAW:
                return map_core_cluster_name(core_no);
            case PERF_TYPE_SOFTWARE:
                return "software";
            case PERF_TYPE_TRACEPOINT:
                return "tracepoint";
            default: {
                if (type < PERF_TYPE_MAX) {
                    return "?";
                }
                return map_custom_pmu_type(type, core_no);
            }
        }
    }

    std::string perf_event_printer_t::perf_attr_to_string(perf_event_attr const & attr,
                                                          core_no_t core_no,
                                                          char const * indentation,
                                                          char const * separator)
    {
        return (lib::Format() << indentation << "type: " << attr.type                                          //
                              << " (" << map_attr_type(attr.type, core_no) << ")" << separator                 //
                              << indentation << "config: " << attr.config << separator                         //
                              << indentation << "config1: " << attr.config1 << separator                       //
                              << indentation << "config2: " << attr.config2 << separator                       //
                              << indentation << "sample: " << attr.sample_period << separator << std::hex      //
                              << indentation << "sample_type: 0x" << attr.sample_type << separator             //
                              << indentation << "read_format: 0x" << attr.read_format << separator << std::dec //
                              << indentation << "pinned: " << (attr.pinned ? "true" : "false") << separator    //
                              << indentation << "mmap: " << (attr.mmap ? "true" : "false") << separator        //
                              << indentation << "comm: " << (attr.comm ? "true" : "false") << separator        //
                              << indentation << "freq: " << (attr.freq ? "true" : "false") << separator        //
                              << indentation << "task: " << (attr.task ? "true" : "false") << separator        //
                              << indentation << "exclude_kernel: " << (attr.exclude_kernel ? "true" : "false")
                              << separator //
                              << indentation << "enable_on_exec: " << (attr.enable_on_exec ? "true" : "false")
                              << separator                                                                    //
                              << indentation << "inherit: " << (attr.inherit ? "true" : "false") << separator //
                              << indentation << "sample_id_all: " << (attr.sample_id_all ? "true" : "false")
                              << separator //
                              << indentation << "sample_regs_user: 0x" << std::hex << attr.sample_regs_user << separator
                              << std::dec //
                              << indentation << "aux_watermark: " << attr.aux_watermark << separator);
    }
}
