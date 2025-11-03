/* Copyright (C) 2021-2025 by Arm Limited. All rights reserved. */

#pragma once

#include <optional>
#include <string>

namespace gator::android {

    /**
     * @brief Copies a file to the data directory of the specified Android package.
     * @param package The Android package name.
     * @param src_path The source file path to copy from.
     * @param dst_filename The destination filename in the package's data directory.
     * @return std::optional<std::string> The full path to the copied file in the package's data directory,
     */
    std::optional<std::string> copy_to_pkg_data_dir(const std::string & package,
                                                    const std::string & src_path,
                                                    const std::string & dst_filename);

    /**
     * @brief Removes a file from the data directory of the specified Android package.
     * @param package The Android package name.
     * @param filename The filename in the package's data directory to remove.
     * @return int The exit code of the remove command (0 on success, non-zero on failure).
     */
    int remove_from_pkg_data_dir(const std::string & package, const std::string & filename);

    /**
     * @brief Copies the gator executable the Android app's home folder and
     * returns its full path.
     *
     * @param package_name The package to copy into.
     * @return std::optional<std::string> The path to the copied gator binary
     *   or nullopt if the copy failed.
     */
    std::optional<std::string> deploy_to_package(const std::string & package_name);

}
