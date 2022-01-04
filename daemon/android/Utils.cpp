/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#include "android/Utils.h"

#include "GatorCLIParser.h"
#include "LocalCapture.h"
#include "Logging.h"
#include "lib/FileDescriptor.h"
#include "lib/FsEntry.h"
#include "lib/Popen.h"
#include "lib/Process.h"
#include "lib/Syscall.h"

#include <array>
#include <cerrno>
#include <iostream>
#include <optional>

namespace android_utils {

    constexpr std::string_view ARG_SHORT_OPTION_START = "-";
    constexpr std::string_view WHITE_SPACE = " ";

    bool isArgMatched(const std::string & arg, const struct option & option)
    {
        return (arg == option.name) || (arg.length() == 1 && arg[0] == (char) option.val);
    }

    std::optional<std::vector<std::string>> getGatorArgsWithAndroidOptionsReplaced(
        const std::vector<std::pair<std::string, std::optional<std::string>>> & gatorArgValuePairs)
    {
        std::vector<std::string> result;
        bool hasPackageName = false;
        bool hasWaitProcessOrApp = false;
        bool hasPackageArg = false;

        for (auto const & argValuePair : gatorArgValuePairs) {
            auto arg = argValuePair.first;
            auto val = argValuePair.second;

            if (isArgMatched(arg, WAIT_PROCESS) || isArgMatched(arg, APP)) {
                hasWaitProcessOrApp = true;
                break;
            }
            if (isArgMatched(arg, ANDROID_PACKAGE)) {
                hasPackageArg = true;
                if (!val.has_value() || val.value().empty()
                    || val.value().find_first_not_of(WHITE_SPACE.data()) == std::string::npos) {
                    break;
                }
                hasPackageName = true;
                result.push_back(ARG_SHORT_OPTION_START.data() + std::string(1, (char) WAIT_PROCESS.val));
                result.push_back(val.value());
            }
            else if (!isArgMatched(arg, ANDROID_ACTIVITY)) {
                result.push_back(ARG_SHORT_OPTION_START.data() + arg);
                if (val) {
                    result.push_back(val.value());
                }
            }
        }
        if (hasWaitProcessOrApp || !hasPackageName || !hasPackageArg) {
            return std::nullopt;
        }
        return {result};
    }

    bool copyApcToActualPath(const std::string & androidPackageName, const std::string & apcPathInCmdLine)
    {
        auto canCreateApc = canCreateApcDirectory(apcPathInCmdLine);
        if (!canCreateApc) {
            return false;
        }
        auto origApcDir = lib::FsEntry::create(canCreateApc.value());

        const auto targetTarFile = origApcDir.path() + ".tar";
        auto targetTarFileFsEnrty = lib::FsEntry::create(targetTarFile);

        //create tar copy apc to cmdline output path
        int copyResult =
            gator::process::runCommandAndRedirectOutput("run-as " + androidPackageName + " tar -c " + origApcDir.name(),
                                                        std::make_optional(targetTarFile));
        if (copyResult != 0) {
            LOG_DEBUG("Zip tar file '/data/data/%s/%s' to '%s' failed ",
                      androidPackageName.c_str(),
                      origApcDir.name().c_str(),
                      targetTarFile.c_str());
            return false;
        }
        auto destination = origApcDir.parent() ? origApcDir.parent().value().path() : "/data/local/tmp";
        //unzip tar
        auto unzipResult =
            gator::process::runCommandAndRedirectOutput("tar -xf " + targetTarFile + " -C " + destination,
                                                        std::nullopt);
        if (unzipResult != 0) {
            LOG_DEBUG("Unzipping tar file '%s' to '%s' failed ", targetTarFile.c_str(), destination.c_str());
        }
        gator::process::runCommandAndRedirectOutput("run-as " + androidPackageName + " rm -r " + origApcDir.name(),
                                                    std::nullopt);
        if (!targetTarFileFsEnrty.remove()) {
            LOG_DEBUG("Failed to removed tar file '%s'", targetTarFileFsEnrty.path().c_str());
        }
        return unzipResult == 0;
    }

    std::optional<std::string> getApcFolderInAndroidPackage(const std::string & appCwd,
                                                            const std::string & targetApcPath)
    {
        const auto origApcDir = lib::FsEntry::create(targetApcPath);
        if (origApcDir.name().empty()) {
            return std::nullopt;
        }
        return appCwd + "/" + origApcDir.name();
    }

    std::optional<std::string> canCreateApcDirectory(const std::string & targetApcPath)
    {
        auto apcDir = lib::FsEntry::create(targetApcPath);
        std::string apcPathWithEtn(apcDir.path());
        //check if path ends with .apc, if not append .apc to folder
        if (apcPathWithEtn.size() > 0
            && apcPathWithEtn.compare(apcPathWithEtn.size() - 4, apcPathWithEtn.size(), ".apc") != 0) {
            apcPathWithEtn += ".apc";
        }
        auto origApcDir = lib::FsEntry::create(apcPathWithEtn);
        if (origApcDir.exists()) {
            origApcDir.remove_all();
            if (origApcDir.exists()) {
                LOG_DEBUG("Desitination folder exists '%s' and could not be deleted.", apcPathWithEtn.c_str());
                return {};
            }
        }
        if (!origApcDir.parent() || !origApcDir.parent().value().exists()) {
            if (!origApcDir.create_directory()) {
                LOG_ERROR("Failed to create a destination folder '%s'.", apcPathWithEtn.c_str());
                return {};
            };
        }
        return {origApcDir.path()};
    }
}
