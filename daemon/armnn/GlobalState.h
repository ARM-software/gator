/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */
#pragma once

#include "../Events.h"
#include "IGlobalState.h"

#include <functional>
#include <mutex>
#include <vector>

namespace armnn {
    class GlobalState : IGlobalState {
    public:
        GlobalState();

        // ------------- IGlobalState --------------------- //

        /** @return A map from global event id to APC counter key */
        virtual const std::map<armnn::EventId, int> & getRequestedCounters() const override;
        /** @return True if capture is active, false if not */
        virtual bool isCaptureActive() const override;
        /** @return The requested capture mode */
        virtual CaptureMode getCaptureMode() const override;
        /** @return The requested sample period */
        virtual std::uint32_t getSamplePeriod() const override;

        virtual void setSessionEvents(const void * sessionState, std::vector<std::tuple<EventId, EventProperties>>) override;

        virtual void removeSessionState(const void * sessionState) override;

        std::vector<Category> getCategories() const;

    private:
        std::map<EventId, int> counterKeyByGlobalId;
        mutable std::mutex eventsMutex;

        std::map<std::string, Category> categoriesByName;
    };

}
