/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#include "GatorAndroidSetupHandler.h"

#include "GatorException.h"
#include "Logging.h"
#include "PropertyUtils.h"

#include <utility>

using namespace gator::android;

namespace {
    constexpr std::string_view DEBUG_PERF_EBENT_MBLOCK_KB = "8196";
    constexpr std::string_view SECURITY_PERF_HARDEN = "0";
    constexpr std::string_view SECURITY_PERF_HARDEN_OFF = "1";

    constexpr std::string_view DEBUG_PERF_EVENT_MBLOCK_PROP = "debug.perf_event_mlock_kb";
    constexpr std::string_view SECURITY_PERF_HIDDEN_PROP = "security.perf_harden";
}

GatorAndroidSetupHandler::GatorAndroidSetupHandler(SessionData & sessionData, UserClassification userClassification)
    : LinuxEnvironmentConfig(sessionData)
{
    auto propSecurityperfHarden = android_prop_utils::readProperty(SECURITY_PERF_HIDDEN_PROP);
    if (propSecurityperfHarden) {
        initialPropertyMap.emplace(SECURITY_PERF_HIDDEN_PROP, std::move(*propSecurityperfHarden));
    }

    // clear it so that the update later triggers the others to change
    android_prop_utils::setProperty(SECURITY_PERF_HIDDEN_PROP, SECURITY_PERF_HARDEN_OFF);

    auto propMlockKb = android_prop_utils::readProperty(DEBUG_PERF_EVENT_MBLOCK_PROP);
    if (propMlockKb) {
        initialPropertyMap.emplace(DEBUG_PERF_EVENT_MBLOCK_PROP, std::move(*propMlockKb));
    }

    android_prop_utils::setProperty(DEBUG_PERF_EVENT_MBLOCK_PROP, DEBUG_PERF_EBENT_MBLOCK_KB);

    // always last since it modifies the others
    if (!android_prop_utils::setProperty(SECURITY_PERF_HIDDEN_PROP, SECURITY_PERF_HARDEN)) {
        // only an error if running as root/shell
        if ((userClassification == UserClassification::root) || (userClassification == UserClassification::shell)) {
            throw GatorException("Unable to set security.perf_harden property. Capture will not be possible.");
        }
    }
}

GatorAndroidSetupHandler::~GatorAndroidSetupHandler() noexcept
{
    auto propMlockKbP = initialPropertyMap.find(DEBUG_PERF_EVENT_MBLOCK_PROP);
    if (propMlockKbP != initialPropertyMap.end()) {
        android_prop_utils::setProperty(DEBUG_PERF_EVENT_MBLOCK_PROP, std::move(propMlockKbP->second));
        initialPropertyMap.erase(DEBUG_PERF_EVENT_MBLOCK_PROP);
    }

    // always last, since it updates the others
    auto propSecurityperfHarden = initialPropertyMap.find(SECURITY_PERF_HIDDEN_PROP);
    if (propSecurityperfHarden != initialPropertyMap.end()) {
        android_prop_utils::setProperty(SECURITY_PERF_HIDDEN_PROP, std::move(propSecurityperfHarden->second));
        initialPropertyMap.erase(SECURITY_PERF_HIDDEN_PROP);
    }
}
