/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */

#include "android/IAppGatorRunner.h"
#include "lib/Popen.h"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace gator::android {
    class AppGatorRunner : public IAppGatorRunner {

    public:
        AppGatorRunner(const std::string & gatorExePath,
                       const std::string & appName,
                       const std::string & gatorAgentName)
            : gatorExePath(gatorExePath), appName(appName), gatorAgentName(gatorAgentName)
        {
        }

        /**
         * This will do a pclose for the file descriptors that were opened when the app
         * gator was started and remove gatord from package
         */
        virtual ~AppGatorRunner() noexcept;

        /**
         * Start app gator with the app/package name
         * @return - PopenResult with the fds created, nullopt other wise
         *           Also returns nullopt, process was already started
         *           and still running, ie pclose() was not called.
         */
        std::optional<const lib::PopenResult> startGator(const ArgsList & gatorArgs) override;

        /**
         *
         * @param message - any message to be send to app gator
         * @return if write failed return false, else true.
         */
        bool sendMessageToAppGator(const std::string & message) const override;

        /**
         * Sends a POSIX signal to the child process.
         * @param signum - any signals to app gator
         * @return - if failed to send will return false
         */
        bool sendSignalsToAppGator(const int signum) const override;

    private:
        const std::string gatorExePath;
        const std::string appName;
        const std::string gatorAgentName;
        std::optional<lib::PopenResult> popenRunAsResult;
        std::optional<std::string> gatorArgsUsed;

        /**
         * This will do a pclose for the file descriptors that were opened when the app
         * gator was started.
         * @return if pclose failed return false, else true.
         */
        bool closeAppGatorDescriptors();

        /**
         * Remove gatord from the package.
         * @return true if successfully removed.
         */
        bool removeGator() const;
    };
}
