/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#pragma once

#include "lib/Popen.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gator::android {
    class IAppGatorRunner {
    public:
        using ArgsList = std::vector<std::pair<std::string, std::optional<std::string>>>;

        virtual ~IAppGatorRunner() = default;

        /**
         * Start app gator with the app/package name
         * @return - PopenResult with the fds created, nullopt other wise
         *           Also returns nullopt, process was already started
         *           and still running, ie pclose() was not called.
         */
        virtual std::optional<const lib::PopenResult> startGator(const ArgsList & args) = 0;

        /**
         *
         * @param message - any message to be send to app gator
         * @return if write failed return false, else true.
         */
        virtual bool sendMessageToAppGator(const std::string & message) const = 0;

        /**
         * Sends a POSIX signal to the child process.
         * @param signum - any signals to app gator
         * @return - if failed to send will return false
         */
        virtual bool sendSignalsToAppGator(const int signum) const = 0;
    };

    /**
     * @brief Factory function to create AppGatorRunner instances. This is just
     * here to simplify testing.
     *
     * @param gator_exe_path Path to the app gator executable.
     * @param package_name The package name of the android app.
     * @param agent_name The agent name to be passed as a command line ption to the new process.
     * @return std::unique_ptr<IAppGatorRunner>
     */
    extern std::unique_ptr<IAppGatorRunner> create_app_gator_runner(const std::string & gator_exe_path,
                                                                    const std::string & package_name,
                                                                    const std::string & agent_name);
}
