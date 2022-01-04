/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#ifndef ANDROID_PROP_UTILS_H_
#define ANDROID_PROP_UTILS_H_

#include <optional>
#include <string>
#include <string_view>

namespace android_prop_utils {

    std::optional<std::string> readProperty(std::string_view prop, bool singleLine = true);
    bool setProperty(std::string_view prop, std::string_view value);
}
#endif /* ANDROID_PROP_UTILS_H_ */
