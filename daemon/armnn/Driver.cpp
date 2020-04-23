/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#include "Driver.h"

#include "../xml/EventsXMLProcessor.h"

namespace armnn {
    Driver::Driver() : ::Driver("ArmNN Driver"), globalState() {}

    // Returns true if this driver can manage the counter
    bool Driver::claimCounter(Counter & counter) const
    {
        return false; // TODO:
    }

    // Clears and disables all counters/SPE
    void Driver::resetCounters()
    {
        // TODO:
    }

    // Enables and prepares the counter for capture
    void Driver::setupCounter(Counter & counter)
    {
        // TODO:
    }

    // Emits available counters
    int Driver::writeCounters(mxml_node_t * const root) const
    {
        // TODO:
        return 0;
    }

    // Emits possible dynamically generated events/counters
    void Driver::writeEvents(mxml_node_t * const eventsNode) const
    {
        for (const Category & category : globalState.getCategories()) {
            auto categoryAndCounterSet = events_xml::createCategoryAndCounterSetNodes(category);
            // counter set must come before category
            if (categoryAndCounterSet.second != nullptr) {
                mxmlAdd(eventsNode, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, categoryAndCounterSet.second.release());
            }
            mxmlAdd(eventsNode, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, categoryAndCounterSet.first.release());
        }
    }

}
