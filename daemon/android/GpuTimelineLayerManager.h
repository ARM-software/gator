/* Copyright (C) 2025 by Arm Limited (or its affiliates). All rights reserved. */

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace gator::android {

    class GpuTimelineLayerManager {
    public:
        static GpuTimelineLayerManager & getInstance()
        {
            static GpuTimelineLayerManager instance;
            return instance;
        }

        GpuTimelineLayerManager(const GpuTimelineLayerManager &) = delete;
        GpuTimelineLayerManager & operator=(const GpuTimelineLayerManager &) = delete;

        /**
         * @brief Copies the GPUTimeline layer driver to the specified Android package.
         *
         * @param package The package name to copy the layer driver into.
         */
        void deploy_to_package(const std::string & package_name);

        /**
         * @brief  Cleans the GPUTimeline layer driver from a package, if it has been deployed
         *  before and resets the Android settings to their original values
         *
         */
        void clean_from_package();

    private:
        static constexpr std::string_view LAYER_DRIVER_LIB {"libVkLayerGPUTimeline.so"};
        static constexpr std::string_view LAYER_DRIVER_NAME {"VK_LAYER_LGL_gpu_timeline"};

        GpuTimelineLayerManager() = default;
        ~GpuTimelineLayerManager() = default;

        bool layer_deployed = false;
        std::optional<std::string> orig_enable_gpu_debug_layers;
        std::optional<std::string> orig_gpu_debug_app;
        std::optional<std::string> orig_gpu_debug_layer_app;
        std::optional<std::string> orig_gpu_debug_layers;
        std::string package;
    };

}
