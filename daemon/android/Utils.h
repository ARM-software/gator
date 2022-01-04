/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#ifndef ANDROID_UTILS_H_
#define ANDROID_UTILS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace android_utils {
    constexpr std::string_view RUN_AS = "run-as";

    /**
     * Replaces android_package and android_activity arguments
     * with wait_process <packagename>.
     *
     * @param gatorArgValuePairs - vector of arg to value pairs of gatord command line options.
     * @return - returns a list of strings representing the newly reconstructed arguments
     *           for gatord, or nullopt if an error occurred.
     */
    std::optional<std::vector<std::string>> getGatorArgsWithAndroidOptionsReplaced(
        const std::vector<std::pair<std::string, std::optional<std::string>>> & gatorArgValuePairs);
    /**
     * Copies apc created in package while running as app gator to apc path given in command line
     * @param androidPackageName - android package name, where the apc is created
     * @param apcPathInCmdLine - apc to be created as mentioned in command line
     * @return - returns true if the copy was successfull , false otherwise
     */
    bool copyApcToActualPath(const std::string & androidPackageName, const std::string & apcPathInCmdLine);

    /**
     * Returns a path with the apc folder inside the android package
     * @param appCwd - cwd
     * @param targetApcPath - path to create apc as mentioned in command line
     * @return - returns path to apc in the  package is an apc folder name exits , else nullopt
     */
    std::optional<std::string> getApcFolderInAndroidPackage(const std::string & appCwd,
                                                            const std::string & targetApcPath);

    /**
     * Checks if the apc dir can be created. If the directory already exists it will be removed
     * and the apc dir will be created.
     * @param targetApcPath - path to create apc as mentioned in command line
     * @return - path to the apc if created , else nullopt
     */
    std::optional<std::string> canCreateApcDirectory(const std::string & targetApcPath);
}

#endif /* ANDROID_UTILS_H_ */
