/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#include "android/AndroidActivityManager.h"

#include "Logging.h"
#include "lib/FileDescriptor.h"
#include "lib/Popen.h"

#include <array>
#include <cstring>
#include <functional>
#include <sstream>
#include <string_view>

namespace {
    constexpr std::string_view PM = "pm";
    constexpr std::string_view LIST = "list";
    constexpr std::string_view PKGS = "packages";
    constexpr std::string_view AM = "am";
    constexpr std::string_view START_ACT = "start-activity";
    constexpr std::string_view FORCE_STOP = "force-stop";
    constexpr std::size_t CMD_BUF_SIZE = 128;
    constexpr std::size_t STARTACT_ERR_BUF_SIZE = 256;
    constexpr std::size_t LIST_CMD_LEN = 5;

    bool closeSuccessfully(const lib::PopenResult & result)
    {
        auto status = lib::pclose(result);
        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        if (!WIFEXITED(status)) {
            return false;
        }
        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        const int exitCode = WEXITSTATUS(status);
        return exitCode == 0;
    }

    template<std::size_t ARR_SIZE>
    std::string getCommandAsString(std::array<const char *, ARR_SIZE> cmd)
    {
        std::stringstream ss;
        for (std::size_t i = 0; i < ARR_SIZE; ++i) {
            if (cmd[i] == nullptr) {
                break;
            }
            if (i) {
                ss << " ";
            }
            ss << cmd[i];
        }
        return ss.str();
    }

    template<std::size_t ARR_SIZE>
    bool executeCommand(const std::array<const char *, ARR_SIZE> & cmd,
                        const std::function<void(int,int)> & readFunction)
    {
        const lib::PopenResult result = lib::popen(cmd);

        if (result.pid < 0) {
            LOG_ERROR("lib::popen failed to execute command (%s)", getCommandAsString(cmd).c_str());
            return false;
        }

        readFunction(result.out, result.err);

        if (!closeSuccessfully(result)) {
            LOG_ERROR("lib::pclose failed to exit (%s).", getCommandAsString(cmd).c_str());
            return false;
        }

        return true;
    }

    template<std::size_t ARR_SIZE, std::size_t BUF_SIZE>
    bool executeCommandAndReadOutput(const std::array<const char *, ARR_SIZE> & cmd,
                                     std::array<char, BUF_SIZE> & output)
    {
        return executeCommand(cmd, [&]([[maybe_unused]] int outfd, [[maybe_unused]] int errfd) { lib::readAll(outfd, output.data(), BUF_SIZE); });
    }

    template<std::size_t ARR_SIZE, std::size_t BUF_SIZE>
    bool executeCommandAndReadErrors(const std::array<const char *, ARR_SIZE> & cmd,
                                     std::array<char, BUF_SIZE> & output)
    {
        return executeCommand(cmd, [&]([[maybe_unused]] int outfd, [[maybe_unused]] int errfd) { lib::readAll(errfd, output.data(), BUF_SIZE); });
    }

    template<std::size_t ARR_SIZE>
    bool executeCommandSuccessfully(const std::array<const char *, ARR_SIZE> & cmd)
    {
        return executeCommand(cmd, []([[maybe_unused]] int outfd, [[maybe_unused]] int errfd) { }); // Do nothing for the read action
    }

    bool hasPackage(const std::string & pkg)
    {
        std::array<const char *, LIST_CMD_LEN> cmd = {PM.data(), LIST.data(), PKGS.data(), pkg.c_str(), nullptr};
        std::array<char, CMD_BUF_SIZE> output;

        if (!executeCommandAndReadOutput(cmd, output)) {
            return false;
        }

        if (strstr(output.data(), pkg.c_str()) == nullptr) {
            LOG_ERROR("The specified package(%s) is not installed.", pkg.c_str());
            return false;
        }
        return true;
    }
}

std::unique_ptr<IAndroidActivityManager> create_android_activity_manager(const std::string & package_name,
                                                                         const std::string & activity_name)
{
    return AndroidActivityManager::create(package_name, activity_name);
}

std::unique_ptr<IAndroidActivityManager> AndroidActivityManager::create(const std::string & pkg,
                                                                        const std::string & activity)
{
    if (pkg.empty() || activity.empty()) {
        LOG_ERROR("A package name and activity name is required.");
        return nullptr;
    }

    if (!hasPackage(pkg)) {
        return nullptr;
    }

    return std::make_unique<AndroidActivityManager>(pkg, activity);
}

AndroidActivityManager::AndroidActivityManager(std::string pkg, std::string activity)
    : pkgName {std::move(pkg)}, actName {std::move(activity)}
{
}

bool AndroidActivityManager::start()
{
    std::string pkgActName = pkgName + "/" + actName;
    std::array<const char *, 4> cmd = {AM.data(), START_ACT.data(), pkgActName.c_str(), nullptr};

    std::array<char, STARTACT_ERR_BUF_SIZE> errorBuffer{};
    auto success = executeCommandAndReadErrors(cmd, errorBuffer);
    errorBuffer[STARTACT_ERR_BUF_SIZE - 1] = '\0';

    // am start-activity always exits with a 0 exit code.
    // "Error type 3" is the way am tells eclipse that the app couldn't be started so check for this string instead.
    if (strstr(errorBuffer.data(), "Error type 3") != nullptr)
    {
        LOG_ERROR("Error starting the specified application(%s). "
            "Make sure the --android-pkg and --android-activity arguments are correct.", pkgActName.c_str());
        return false;
    }
    return success;
}

bool AndroidActivityManager::stop()
{
    std::array<const char *, 4> cmd = {AM.data(), FORCE_STOP.data(), pkgName.c_str(), nullptr};

    return executeCommandSuccessfully(cmd);
}
