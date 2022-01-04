/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#include "AppGatorRunner.h"

#include "Logging.h"
#include "android/Spawn.h"
#include "android/Utils.h"
#include "lib/Popen.h"
#include "lib/Syscall.h"

#include <array>
#include <csignal>
#include <cstring>
#include <iostream>
#include <numeric>
#include <sstream>

namespace gator::android {
    constexpr std::string_view RUN_AS = "run-as";
    constexpr std::string_view KILL = "kill";
    constexpr std::string_view RM = "rm";
    constexpr std::string_view FORCE = "-f";

    std::unique_ptr<IAppGatorRunner> create_app_gator_runner(const std::string & gator_exe_path,
                                                             const std::string & package_name,
                                                             const std::string & activity_name)
    {
        return std::make_unique<AppGatorRunner>(gator_exe_path, package_name, activity_name);
    }

    std::string getArgsJoined(const std::vector<std::string> & args)
    {
        return std::accumulate(std::begin(args),
                               std::end(args),
                               std::string(),
                               [](const std::string & a, const std::string & b) { return a + " " + b; });
    }

    std::optional<const lib::PopenResult> AppGatorRunner::startGator(const ArgsList & gatorArgs)
    {
        auto gatorNewArgs = android_utils::getGatorArgsWithAndroidOptionsReplaced(gatorArgs);
        if (!gatorNewArgs) {
            LOG_ERROR("Failed to replace android args with wait process");
            return std::nullopt;
        }

        if (popenRunAsResult.has_value()) {
            LOG_ERROR("Cannot start, application gator already started with '%s %s%s'", //
                      gatorExePath.c_str(),
                      gatorAgentName.c_str(),
                      gatorArgsUsed.has_value() ? gatorArgsUsed.value().c_str() : "");
            return std::nullopt;
        }

        std::vector<const char *> argumentsToStartGator;
        argumentsToStartGator.push_back(RUN_AS.data());
        argumentsToStartGator.push_back(appName.c_str());
#ifdef APP_GATOR_GDB_SERVER
        argumentsToStartGator.push_back("./gdbserver");
        argumentsToStartGator.push_back(":5001");
#endif
        argumentsToStartGator.push_back(gatorExePath.c_str());
        argumentsToStartGator.push_back(gatorAgentName.c_str());

        for (auto & i : gatorNewArgs.value()) {
            argumentsToStartGator.push_back(i.c_str());
        }
        argumentsToStartGator.push_back(nullptr);

        auto gatorCommand = lib::makeConstSpan(argumentsToStartGator);
        auto runGatorCommandResult = lib::popen(gatorCommand);

        std::string argsJoined = getArgsJoined(gatorNewArgs.value());
        gatorArgsUsed = std::make_optional(argsJoined);

        if (runGatorCommandResult.pid < 0) {
            LOG_ERROR("lib::popen(%s %s %s %s%s) failed , (errno = %d)",
                      RUN_AS.data(),
                      appName.c_str(),
                      gatorExePath.c_str(),
                      gatorAgentName.c_str(),
                      argsJoined.c_str(),
                      -runGatorCommandResult.pid);
            return std::nullopt;
        }
        popenRunAsResult = std::make_optional(runGatorCommandResult);
        return runGatorCommandResult;
    }

    bool AppGatorRunner::sendMessageToAppGator(const std::string & message) const
    {
        if (!popenRunAsResult.has_value()) {
            LOG_DEBUG("No PopenResult returned while starting app gator");
            return false;
        }
        auto bytesWritten = lib::write(popenRunAsResult->in, message.c_str(), message.length());
        if (bytesWritten < 0) {
            LOG_DEBUG("Error while writing message (%s)", message.c_str());
            return false;
        }
        if (static_cast<unsigned long>(bytesWritten) != message.length()) {
            LOG_DEBUG("Message written length varies actual(%zu) expected(%zu)", bytesWritten, strlen(message.c_str()));
            return false;
        }
        return true;
    }

    bool AppGatorRunner::sendSignalsToAppGator(const int signum) const
    {
        if (!popenRunAsResult.has_value()) {
            LOG_DEBUG("No PopenResult returned while starting app gator");
            return false;
        }

        auto pid_str = std::to_string(popenRunAsResult->pid);

        std::array<char, 8> sig_buf {0};
        std::snprintf(sig_buf.data(), sig_buf.size(), "-%d", signum);

        std::vector<const char *> argumentsToSendSignal;
        argumentsToSendSignal.push_back(RUN_AS.data());
        argumentsToSendSignal.push_back(appName.c_str());
        argumentsToSendSignal.push_back(KILL.data());
        argumentsToSendSignal.push_back(sig_buf.data());
        argumentsToSendSignal.push_back(pid_str.c_str());
        argumentsToSendSignal.push_back(nullptr);

        auto result = lib::popen(argumentsToSendSignal);
        if (result.pid < 0) {
            LOG_DEBUG("Failed to send signal %d to gator agent process with pid %d", signum, popenRunAsResult->pid);
            return false;
        }
        const int status = lib::pclose(result);
        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        if (!WIFEXITED(status)) {
            LOG_DEBUG("'%s %s %s %s %s' exited abnormally",
                      RUN_AS.data(),
                      appName.c_str(), //
                      KILL.data(),
                      sig_buf.data(),
                      pid_str.c_str());
            return false;
        }
        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        const int exitCode = WEXITSTATUS(status);
        if (exitCode != 0) {
            LOG_DEBUG("'%s %s %s %s %s' failed: %d",
                      RUN_AS.data(),
                      appName.c_str(), //
                      KILL.data(),
                      sig_buf.data(),
                      pid_str.c_str(),
                      exitCode);
            return false;
        }
        return true;
    }

    bool AppGatorRunner::closeAppGatorDescriptors()
    {
        if (!popenRunAsResult.has_value()) {
            LOG_DEBUG("No PopenResult returned while starting app gator");
            return false;
        }
        const int status = lib::pclose(popenRunAsResult.value());

        popenRunAsResult.reset();
        auto cmdUsed = std::move(gatorArgsUsed);
        gatorArgsUsed.reset();

        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        if (!WIFEXITED(status)) {
            LOG_DEBUG("'%s %s %s %s%s' exited abnormally",
                      RUN_AS.data(),
                      appName.c_str(), //
                      gatorExePath.c_str(),
                      gatorAgentName.c_str(),
                      cmdUsed.has_value() ? cmdUsed.value().c_str() : "");
            return false;
        }
        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        const int exitCode = WEXITSTATUS(status);
        if (exitCode != 0) {
            LOG_DEBUG("'%s %s %s %s%s' failed: %d",
                      RUN_AS.data(),
                      appName.c_str(), //
                      gatorExePath.c_str(),
                      gatorAgentName.c_str(),
                      (cmdUsed.has_value() ? cmdUsed.value().c_str() : ""),
                      exitCode);
            return false;
        }
        return true;
    }

    bool AppGatorRunner::removeGator() const
    {
        if (popenRunAsResult.has_value()) {
            LOG_DEBUG("Gatord has file descriptors that are not closed (pi = %d). Try closeAppGatorDescriptors() "
                      "before removing "
                      "gatord.",
                      popenRunAsResult->pid);
            return false;
        }
        std::vector<const char *> argumentsToRemoveGator;
        argumentsToRemoveGator.push_back(RUN_AS.data());
        argumentsToRemoveGator.push_back(appName.c_str());
        argumentsToRemoveGator.push_back(RM.data());
        argumentsToRemoveGator.push_back(FORCE.data());
        argumentsToRemoveGator.push_back(gatorExePath.c_str());
        argumentsToRemoveGator.push_back(nullptr);

        auto result = lib::popen(argumentsToRemoveGator);

        if (result.pid < 0) {
            LOG_DEBUG("Failed to remove gatord at %s , errno %d", gatorExePath.c_str(), -result.pid);
            return false;
        }
        const int status = lib::pclose(result);
        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        if (!WIFEXITED(status)) {
            LOG_DEBUG("'%s %s %s %s %s' exited abnormally",
                      RUN_AS.data(),
                      appName.c_str(), //
                      RM.data(),
                      FORCE.data(),
                      gatorExePath.c_str());
            return false;
        }
        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        const int exitCode = WEXITSTATUS(status);
        if (exitCode != 0) {
            LOG_DEBUG("'%s %s %s %s %s' failed: %d",
                      RUN_AS.data(),
                      appName.c_str(), //
                      RM.data(),
                      FORCE.data(),
                      gatorExePath.c_str(),
                      exitCode);
            return false;
        }
        return true;
    }

    AppGatorRunner::~AppGatorRunner() noexcept
    {
        closeAppGatorDescriptors();
        removeGator();
    }
}
