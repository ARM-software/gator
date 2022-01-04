/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#pragma once

#include <memory>
#include <string>

/**
 * Implementations act as a wrapper for starting and killing android package activities
 */
class IAndroidActivityManager {
public:
    virtual ~IAndroidActivityManager() = default;

    /**
     * Starts the activity (non-blocking)
     * @returns true for success, false otherwise
     */
    [[nodiscard]] virtual bool start() = 0;

    /**
     * Stops the activity (blocking)
     * @returns true for success, false otherwise
     */
    [[nodiscard]] virtual bool stop() = 0;
};

extern std::unique_ptr<IAndroidActivityManager> create_android_activity_manager(const std::string & package_name,
                                                                                const std::string & activity_name);

/**
 * Default implementation of IAndroidActivityManager that uses the command line
 * "am" tool to manage processes.
 */
class AndroidActivityManager : public IAndroidActivityManager {
public:
    friend std::unique_ptr<AndroidActivityManager> std::make_unique<AndroidActivityManager>(const std::string &,
                                                                                            const std::string &);

    /**
     * @param pkg the package name (e.g. com.arm.example)
     * @param activity the activity name
     * @returns a unique pointer to an AndroidActivityManager, or nullptr
     */
    static std::unique_ptr<IAndroidActivityManager> create(const std::string & pkg, const std::string & activity);

    virtual ~AndroidActivityManager() = default;

    /**
     * Starts the activity (non-blocking)
     * @returns true for success, false otherwise
     */
    [[nodiscard]] bool start() override;

    /**
     * Stops the activity (blocking)
     * @returns true for success, false otherwise
     */
    [[nodiscard]] bool stop() override;

private:
    const std::string pkgName;
    const std::string actName;

    AndroidActivityManager(std::string pkg, std::string activity);
};
