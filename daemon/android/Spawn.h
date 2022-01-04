/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#pragma once

#include <optional>
#include <string>

namespace gator { namespace android {

    /**
     * @brief Copies the gator executable the Android app's home folder and
     * returns its full path.
     * 
     * @param package_name The package to copy into.
     * @return std::optional<std::string> The path to the copied gator binary
     *   or nullopt if the copy failed.
     */
    std::optional<std::string> deploy_to_package(const std::string & package_name);

} // namespace android
} // namespace gator
