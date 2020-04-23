/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#include "GlobalState.h"

namespace armnn {
    GlobalState::GlobalState() : counterKeyByGlobalId(), eventsMutex(), categoriesByName() {}

    /** @return A map from global event id to APC counter key */
    const std::map<EventId, int> & GlobalState::getRequestedCounters() const { return counterKeyByGlobalId; }

    /** @return True if capture is active, false if not */
    bool GlobalState::isCaptureActive() const
    {
        return false; // TODO:
    }
    /** @return The requested capture mode */
    CaptureMode GlobalState::getCaptureMode() const
    {
        return CaptureMode::PERIOD_CAPTURE; // TODO:
    }
    /** @return The requested sample period */
    std::uint32_t GlobalState::getSamplePeriod() const
    {
        return 0; // TODO:
    }

    void GlobalState::setSessionEvents(const void * sessionState, std::vector<std::tuple<EventId, EventProperties>>)
    {

    }

    void GlobalState::removeSessionState(const void * sessionState)
    {

    }


    std::vector<Category> GlobalState::getCategories() const
    {
        std::lock_guard<std::mutex> lock{eventsMutex};

        std::vector<Category> xmlCategories;

        return xmlCategories;
    }
}
