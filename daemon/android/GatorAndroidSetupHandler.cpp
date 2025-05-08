/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */

#include "GatorAndroidSetupHandler.h"

#include "CpuUtils.h"
#include "GatorException.h"
#include "Logging.h"
#include "SessionData.h"
#include "android/PropertyUtils.h"

#include <string>
#include <string_view>
#include <utility>

using namespace gator::android;
using namespace android_prop_utils;
using namespace cpu_utils;

namespace {
    constexpr std::string_view SECURITY_PERF_HARDEN = "0";
    constexpr std::string_view SECURITY_PERF_HARDEN_OFF = "1";
    constexpr std::string_view DEBUG_PERF_EVENT_MLOCK_PROP = "debug.perf_event_mlock_kb";
    constexpr std::string_view SECURITY_PERF_HARDEN_PROP = "security.perf_harden";

    // a value of "1" ensures traced is enabled
    constexpr std::string_view PERSIST_TRACED_ENABLE = "persist.traced.enable";
    constexpr std::string_view TRACED_ENABLE = "1";

    constexpr int ONE_KB = 1024;
    constexpr int LARGE_BUFFER_CORE_MULTIPLIER = 512;
    constexpr int SMALL_BUFFER_MULTIPLIER = 129; //128 +1
    constexpr int DEBUG_PERF_EVENT_MLOCK_KB = 8196;
}

GatorAndroidSetupHandler::GatorAndroidSetupHandler(UserClassification userClassification)
{
    auto propSecurityperfHarden = readProperty(SECURITY_PERF_HARDEN_PROP);
    if (propSecurityperfHarden) {
        initialPropertyMap.emplace(SECURITY_PERF_HARDEN_PROP, std::move(*propSecurityperfHarden));
    }

    // clear it so that the update later triggers the others to change
    setProperty(SECURITY_PERF_HARDEN_PROP, SECURITY_PERF_HARDEN_OFF);

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

    std::optional<std::string> persistTracedEnable = readProperty(PERSIST_TRACED_ENABLE);
    if (persistTracedEnable) {
        LOG_DEBUG("Existing value for property '%s' = '%s'",
                  PERSIST_TRACED_ENABLE.data(),
                  (*persistTracedEnable).data());
        initialPropertyMap.emplace(PERSIST_TRACED_ENABLE, std::move(*persistTracedEnable));
    }

    if (!setProperty(PERSIST_TRACED_ENABLE, TRACED_ENABLE)) {
        LOG_DEBUG("Could not set property '%s' = '%s', continuing anyway",
                  PERSIST_TRACED_ENABLE.data(),
                  TRACED_ENABLE.data());
    }
    else {
        LOG_DEBUG("Successfully set property '%s' = '%s'", PERSIST_TRACED_ENABLE.data(), TRACED_ENABLE.data());
    }

    // always last since it modifies the others
    if (!setProperty(SECURITY_PERF_HARDEN_PROP, SECURITY_PERF_HARDEN)) {
        // only an error if running as root/shell
        if ((userClassification == UserClassification::root) || (userClassification == UserClassification::shell)) {
            throw GatorException("Unable to set security.perf_harden property. Capture will not be possible.");
        }
    }
}

GatorAndroidSetupHandler::~GatorAndroidSetupHandler() noexcept
{
    auto traced = initialPropertyMap.find(PERSIST_TRACED_ENABLE);
    if (traced != initialPropertyMap.end()) {
        setProperty(PERSIST_TRACED_ENABLE, std::move(traced->second));
        initialPropertyMap.erase(PERSIST_TRACED_ENABLE);
    }

    auto propMlockKbP = initialPropertyMap.find(DEBUG_PERF_EVENT_MLOCK_PROP);
    if (propMlockKbP != initialPropertyMap.end()) {
        setProperty(DEBUG_PERF_EVENT_MLOCK_PROP, std::move(propMlockKbP->second));
        initialPropertyMap.erase(DEBUG_PERF_EVENT_MLOCK_PROP);
    }

    // always last, since it updates the others
    auto propSecurityperfHarden = initialPropertyMap.find(SECURITY_PERF_HARDEN_PROP);
    if (propSecurityperfHarden != initialPropertyMap.end()) {
        setProperty(SECURITY_PERF_HARDEN_PROP, std::move(propSecurityperfHarden->second));
        initialPropertyMap.erase(SECURITY_PERF_HARDEN_PROP);
    }
}
