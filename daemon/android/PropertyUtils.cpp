/* Copyright (C) 2022-2023 by Arm Limited. All rights reserved. */

#include "android/PropertyUtils.h"

#include "Logging.h"
#include "lib/Popen.h"
#include "lib/Syscall.h"

#include <cerrno>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include <sys/types.h>

namespace android_prop_utils {
    constexpr std::string_view GET_PROP = "getprop";
    constexpr std::string_view SET_PROP = "setprop";

    std::optional<std::string> readProperty(std::string_view prop, bool singleLine)
    {
        std::string result;
        const lib::PopenResult getprop = lib::popen(GET_PROP.data(), prop.data());
        if (getprop.pid < 0) {
            LOG_WARNING("lib::popen(%s %s) failed: Probably not android (errno = %d)",
                        GET_PROP.data(),
                        prop.data(),
                        -getprop.pid);
            return {};
        }
        char value = '0';
        ssize_t bytesRead = 0;
        while ((bytesRead = lib::read(getprop.out, &value, 1)) > 0) {
            if (singleLine && ((value == '\n') || (value == '\r') || (value == '\0'))) {
                break;
            }
            result += value;
        }
        lib::pclose(getprop);
        if (bytesRead < 0) { //there was an error reading
            LOG_WARNING("lib::read(), there was an error while reading the property '%s'.", prop.data());
            return {};
        }
        return result;
    }

    bool setProperty(std::string_view prop, std::string_view value)
    {
        const lib::PopenResult setPropResult = lib::popen(SET_PROP.data(), prop.data(), value.data());
        //setprop not found, probably not Android.
        if (setPropResult.pid == -ENOENT) {
            LOG_WARNING("lib::popen(%s %s %s) failed (errno =%d)",
                        SET_PROP.data(),
                        prop.data(),
                        value.data(),
                        -setPropResult.pid);
            return false;
        }
        if (setPropResult.pid < 0) {
            LOG_WARNING("lib::popen(%s %s %s) failed (errno =%d) ",
                        SET_PROP.data(),
                        prop.data(),
                        value.data(),
                        -setPropResult.pid);
            return false;
        }
        const int status = lib::pclose(setPropResult);
        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        if (!WIFEXITED(status)) {
            LOG_WARNING("'%s %s %s' exited abnormally", SET_PROP.data(), prop.data(), value.data());
            return false;
        }
        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        const int exitCode = WEXITSTATUS(status);
        if (exitCode != 0) {
            LOG_WARNING("'%s %s %s' failed: %d", SET_PROP.data(), prop.data(), value.data(), exitCode);
            return false;
        }
        return true;
    }

}
