/* Copyright (C) 2025 by Arm Limited (or its affiliates). All rights reserved. */

#pragma once

#include <string>

namespace gator::android::timeline_layer {

    /**
     * @brief Copies the GPUTimeline layer driver to the specified Android package.
     *
     * @param package The package name to copy the layer driver into.
     */
    void deploy_to_package(const std::string & package_name);

}
