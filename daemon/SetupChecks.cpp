/* Copyright (C) 2025 by Arm Limited. All rights reserved. */

#include "SetupChecks.h"

#include "Logging.h"
#include "lib/Format.h"

bool check_spe_available(setup_warnings_t & setup_warnings, lib::Span<GatorCpu const> cpus)
{
    // Check if SPE is enabled and/or supported

    bool spe_enabled_in_kernel = false;
    bool spe_supported_by_hardware = false;
    for (const auto & cpu : cpus) {
        if (cpu.getSpeName() != nullptr) {
            spe_enabled_in_kernel = true;
        }
        if (cpu.getCpuIsKnownToSupportSPE()) {
            spe_supported_by_hardware = true;
        }
        if (spe_enabled_in_kernel && spe_supported_by_hardware) {
            break;
        }
    }

    if (!spe_enabled_in_kernel) {
        if (!spe_supported_by_hardware) {
            setup_warnings.add_error(std::string(spe_not_supported_error));
            LOG_ERROR(spe_not_supported_error.data());
            return false;
        }
        setup_warnings.add_error(std::string(spe_not_enabled_error));
        LOG_ERROR(spe_not_enabled_error.data());
        return false;
    }

    return true;
}
