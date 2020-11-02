/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#include "armnn/ArmNNDriver.h"

#include "Logging.h"
#include "SessionData.h"
#include "xml/EventsXMLProcessor.h"

namespace armnn {
    Driver::Driver()
        : ::Driver {"ArmNN Driver"},
          mSessionCount {0},
          mGlobalState {&getEventKey},
          mAcceptingSocket {SocketIO::udsServerListen("\0gatord_namespace", false)},
          mDriverSourceIpc {
              mSessionManager}, // This constructor doesn't access mSessionManager so it's okay to not be initialised at this point
          mSessionManager {std::unique_ptr<IAcceptor> {new SocketAcceptor {mAcceptingSocket, createSession}}}
    {
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
        // mSessionManager starts threads that cause undefined behaviour and leaks when we fork
        // but as these threads will be in a steady state (unless we get a connection exactly when we fork),
        // there shouldn't be any threading issues, just a small memory leak.
        mSessionManager.stop();
        logg.logError("Arm NN connection listening disabled due to address or thread sanitizer being enabled.");
#endif
    }

    // Returns true if this driver can manage the counter
    bool Driver::claimCounter(Counter & counter) const
    {
        return mGlobalState.hasCounter(std::string(counter.getType()));
    }

    // Clears and disables all counters/SPE
    void Driver::resetCounters() { mGlobalState.disableAllCounters(); }

    // Enables and prepares the counter for capture
    void Driver::setupCounter(Counter & counter)
    {
        lib::Optional<int> optionalEventNo =
            counter.getEvent() != -1 ? lib::Optional<int>(counter.getEvent()) : lib::Optional<int>();

        int key = mGlobalState.enableCounter(std::string(counter.getType()), optionalEventNo);
        counter.setKey(key);
    }

    // Emits available counters
    int Driver::writeCounters(mxml_node_t * const root) const
    {
        int count = 0;
        std::vector<std::string> counterNames = mGlobalState.getAllCounterNames();
        for (auto & counterName : counterNames) {
            mxml_node_t * node = mxmlNewElement(root, "counter");
            mxmlElementSetAttr(node, "name", counterName.c_str());
            ++count;
        }

        return count;
    }

    // Emits possible dynamically generated events/counters
    void Driver::writeEvents(mxml_node_t * const eventsNode) const
    {
        for (const Category & category : mGlobalState.getCategories()) {
            auto categoryAndCounterSet = events_xml::createCategoryAndCounterSetNodes(category);
            // counter set must come before category
            if (categoryAndCounterSet.second != nullptr) {
                mxmlAdd(eventsNode, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, categoryAndCounterSet.second.release());
            }
            mxmlAdd(eventsNode, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, categoryAndCounterSet.first.release());
        }
    }
}
