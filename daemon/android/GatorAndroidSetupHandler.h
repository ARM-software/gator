/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#ifndef ANDROID_GATORANDROIDSETUPHANDLER_H_
#define ANDROID_GATORANDROIDSETUPHANDLER_H_
#include <map>
#include <string>
#include <string_view>

#include <capture/Environment.h>

namespace gator::android {
    /**
     * This class is responsible for managing gatord security settings
     */

    class GatorAndroidSetupHandler : public capture::LinuxEnvironmentConfig {
    public:
        enum class UserClassification {
            root,
            shell,
            other,
        };

        /**
         * Configure the android security properties
         * debug.perf_event_mlock_kb 8192
         * security.perf_harden 0
         */
        GatorAndroidSetupHandler(SessionData & sessionData, UserClassification userClassification);

        /**
         * Will restore the android security properties
         * debug.perf_event_mlock_kb and security.perf_harden which
         * were configured before profiling.
         * The initial values are saved during configureAndroidSecurityProperties, and will be used for restore.
         */
        virtual ~GatorAndroidSetupHandler() noexcept;

    private:
        std::map<std::string_view, std::string> initialPropertyMap {};
    };
}

#endif /* ANDROID_GATORANDROIDSETUPHANDLER_H_ */
