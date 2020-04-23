/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */
#pragma once

#include "../Driver.h"
#include "GlobalState.h"

namespace armnn {
    class Driver : ::Driver {
    public:
        Driver();

        // Returns true if this driver can manage the counter
        virtual bool claimCounter(Counter & counter) const override;

        // Clears and disables all counters/SPE
        virtual void resetCounters() override;

        // Enables and prepares the counter for capture
        virtual void setupCounter(Counter & counter) override;

        // Emits available counters
        virtual int writeCounters(mxml_node_t * const root) const override;

        // Emits possible dynamically generated events/counters
        virtual void writeEvents(mxml_node_t * const) const override;

    private:
        GlobalState globalState;
    };

}
