/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */

#include "Spawn.h"

#include "Logging.h"
#include "lib/FsEntry.h"
#include "lib/Process.h"

#include <optional>
#include <string>

namespace gp = gator::process;

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

    const auto target_exe_path = "/data/data/" + package + "/" + exe_name;
    const auto cmd = "run-as " + package + " cp -f " + real_path.path() + " " + target_exe_path;
    const auto copy_result = gp::system(cmd);

    if (remove_real_path) {
        real_path.remove();
    }

    if (copy_result == 0) {
        return {target_exe_path};
    }
    return std::nullopt;
}
