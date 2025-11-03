/* Copyright (C) 2021-2025 by Arm Limited (or its affiliates). All rights reserved. */

#include "Spawn.h"

#include "Logging.h"
#include "lib/FsEntry.h"
#include "lib/Process.h"

#include <optional>
#include <string>

namespace gp = gator::process;

std::optional<std::string> gator::android ::copy_to_pkg_data_dir(const std::string & package,
                                                                 const std::string & src_path,
                                                                 const std::string & dst_filename)
{
    const auto target_exe_path = "/data/data/" + package + "/" + dst_filename;
    const auto cmd = "run-as " + package + " cp -f " + src_path + " " + target_exe_path;
    int result = gp::system(cmd);
    return result == 0 ? std::optional<std::string> {target_exe_path} : std::nullopt;
}

int gator::android ::remove_from_pkg_data_dir(const std::string & package, const std::string & filename)
{
    const auto target_path = "/data/data/" + package + "/" + filename;
    return gp::system("run-as " + package + " rm -fr " + target_path);
}

std::optional<std::string> gator::android::deploy_to_package(const std::string & package)
{
    const auto self_exe = lib::FsEntry::create("/proc/self/exe");
    const auto maybe_real_path = self_exe.realpath();
    if (!maybe_real_path) {
        LOG_ERROR("Could not resolve gator's executable path");
        return std::nullopt;
    }

    auto real_path = *maybe_real_path;
    const auto exe_name = real_path.name();

    bool remove_real_path = false;

    if (real_path.path().rfind("/data/local/tmp/", 0) == std::string::npos) {
        const auto data_tmp = lib::FsEntry::create("/data/local/tmp");
        const auto tmp_target = lib::FsEntry::create_unique_file(data_tmp);
        if (!tmp_target) {
            return std::nullopt;
        }

        real_path.copyTo(*tmp_target);
        real_path = *tmp_target;
        remove_real_path = true;
    }

    const auto copy_result = copy_to_pkg_data_dir(package, real_path.path(), exe_name);

    if (remove_real_path) {
        real_path.remove();
    }

    return copy_result;
}
