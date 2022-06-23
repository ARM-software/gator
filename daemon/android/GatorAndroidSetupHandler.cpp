/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#include "GatorAndroidSetupHandler.h"

#include "CpuUtils.h"
#include "GatorException.h"
#include "Logging.h"
#include "android/PropertyUtils.h"

#include <utility>

using namespace gator::android;
using namespace android_prop_utils;
using namespace cpu_utils;

namespace {
    constexpr std::string_view SECURITY_PERF_HARDEN = "0";
    constexpr std::string_view SECURITY_PERF_HARDEN_OFF = "1";
    constexpr std::string_view DEBUG_PERF_EVENT_MLOCK_PROP = "debug.perf_event_mlock_kb";
    constexpr std::string_view SECURITY_PERF_HIDDEN_PROP = "security.perf_harden";

    constexpr int ONE_KB = 1024;
    constexpr int LARGE_BUFFER_CORE_MULTIPLIER = 512;
    constexpr int SMALL_BUFFER_MULTIPLIER = 129; //128 +1
    constexpr int DEBUG_PERF_EVENT_MLOCK_KB = 8196;
}

GatorAndroidSetupHandler::GatorAndroidSetupHandler(UserClassification userClassification)
{
    auto propSecurityperfHarden = readProperty(SECURITY_PERF_HIDDEN_PROP);
    if (propSecurityperfHarden) {
        initialPropertyMap.emplace(SECURITY_PERF_HIDDEN_PROP, std::move(*propSecurityperfHarden));
    }

    // clear it so that the update later triggers the others to change
    setProperty(SECURITY_PERF_HIDDEN_PROP, SECURITY_PERF_HARDEN_OFF);

    auto propMlockKb = readProperty(DEBUG_PERF_EVENT_MLOCK_PROP);
    if (propMlockKb) {
        initialPropertyMap.emplace(DEBUG_PERF_EVENT_MLOCK_PROP, std::move(*propMlockKb));
    }

    setProperty(DEBUG_PERF_EVENT_MLOCK_PROP, std::to_string(DEBUG_PERF_EVENT_MLOCK_KB));
    auto largeBufferSize = ((getMaxCoreNum() * LARGE_BUFFER_CORE_MULTIPLIER) + 1) * (gSessionData.mPageSize / ONE_KB);
    if (largeBufferSize > 0 && largeBufferSize != DEBUG_PERF_EVENT_MLOCK_KB) {
        if (!setProperty(DEBUG_PERF_EVENT_MLOCK_PROP, std::to_string(largeBufferSize))) {
            auto smallerBufferSize = SMALL_BUFFER_MULTIPLIER * (gSessionData.mPageSize / ONE_KB);
            if (smallerBufferSize > 0) {
                setProperty(DEBUG_PERF_EVENT_MLOCK_PROP, std::to_string(smallerBufferSize));
            }
        }
    }
    auto updatedPropMlockKb = readProperty(DEBUG_PERF_EVENT_MLOCK_PROP);
    if (updatedPropMlockKb) {
        LOG_DEBUG("Value for %s is \"%s\"", DEBUG_PERF_EVENT_MLOCK_PROP.data(), updatedPropMlockKb.value().c_str());
    }
    else {
        LOG_DEBUG("No value could be read for %s ", DEBUG_PERF_EVENT_MLOCK_PROP.data());
    }

    // always last since it modifies the others
    if (!setProperty(SECURITY_PERF_HIDDEN_PROP, SECURITY_PERF_HARDEN)) {
        // only an error if running as root/shell
        if ((userClassification == UserClassification::root) || (userClassification == UserClassification::shell)) {
            throw GatorException("Unable to set security.perf_harden property. Capture will not be possible.");
        }
    }
}

GatorAndroidSetupHandler::~GatorAndroidSetupHandler() noexcept
{
    auto propMlockKbP = initialPropertyMap.find(DEBUG_PERF_EVENT_MLOCK_PROP);
    if (propMlockKbP != initialPropertyMap.end()) {
        setProperty(DEBUG_PERF_EVENT_MLOCK_PROP, std::move(propMlockKbP->second));
        initialPropertyMap.erase(DEBUG_PERF_EVENT_MLOCK_PROP);
    }

    // always last, since it updates the others
    auto propSecurityperfHarden = initialPropertyMap.find(SECURITY_PERF_HIDDEN_PROP);
    if (propSecurityperfHarden != initialPropertyMap.end()) {
        setProperty(SECURITY_PERF_HIDDEN_PROP, std::move(propSecurityperfHarden->second));
        initialPropertyMap.erase(SECURITY_PERF_HIDDEN_PROP);
    }
}
