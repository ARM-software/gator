/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "CpuUtils.h"
#include "ICpuInfo.h"
#include "agents/perf/capture_configuration.h"

namespace agents::perf {
    /** Implements the ICpuInfo interface, providing a thin wrapper around the data received in the configuration message and allowing simple rescan of properties */
    class cpu_info_t : public ICpuInfo {
    public:
        explicit cpu_info_t(std::shared_ptr<perf_capture_configuration_t> configuration)
            : configuration(std::move(configuration))
        {
        }

        [[nodiscard]] lib::Span<const int> getCpuIds() const override { return configuration->per_core_cpuids; }

        [[nodiscard]] lib::Span<const GatorCpu> getClusters() const override { return configuration->clusters; }

        [[nodiscard]] lib::Span<const int> getClusterIds() const override
        {
            return configuration->per_core_cluster_index;
        }

        [[nodiscard]] const char * getModelName() const override { return ""; }

        void updateIds(bool /*ignoreOffline*/) override
        {
            cpu_utils::readCpuInfo(true, false, configuration->per_core_cpuids);
            ICpuInfo::updateClusterIds(configuration->per_core_cpuids,
                                       configuration->clusters,
                                       configuration->per_core_cluster_index);
        }

    private:
        std::shared_ptr<perf_capture_configuration_t> configuration;
    };
}
