/* Copyright (C) 2020-2024 by Arm Limited. All rights reserved. */

#include "armnn/ArmNNDriver.h"

#include "Config.h"
#include "Counter.h"
#include "Driver.h"
#include "Events.h"
#include "GetEventKey.h"
#include "armnn/IAcceptor.h"
#include "armnn/SocketAcceptor.h"
#include "armnn/SocketIO.h"
#include "xml/EventsXMLProcessor.h"

#include <memory>
#include <string>
#include <vector>

#include <mxml.h>

namespace armnn {
    Driver::Driver()
        : ::Driver {"ArmNN Driver"},
          mGlobalState {&getEventKey},

#if !CONFIG_ARMNN_AGENT
          mAcceptingSocket {SocketIO::udsServerListen("\0gatord_namespace", true)},
          mSessionManager {std::unique_ptr<IAcceptor> {new SocketAcceptor {mAcceptingSocket, createSession}}},
#else
          mAcceptedSocketQueue {},
          mSessionManager {std::unique_ptr<IAcceptor> {new SocketAcceptor {mAcceptedSocketQueue, createSession}}},
#endif
          mDriverSourceConn {mSessionManager}
    {
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
        // mSessionManager starts threads that cause undefined behaviour and leaks when we fork
        // but as these threads will be in a steady state (unless we get a connection exactly when we fork),
        // there shouldn't be any threading issues, just a small memory leak.
        mSessionManager.stop();
        LOG_ERROR("Arm NN connection listening disabled due to address or thread sanitizer being enabled.");
#else
#if !CONFIG_ARMNN_AGENT
        mSessionManager.start();
#endif
#endif
    }

    // Returns true if this driver can manage the counter
    bool Driver::claimCounter(Counter & counter) const
    {
        return mGlobalState.hasCounter(std::string(counter.getType()));
    }

    // Clears and disables all counters/SPE
    void Driver::resetCounters()
    {
        mGlobalState.disableAllCounters();
    }

    // Enables and prepares the counter for capture
    void Driver::setupCounter(Counter & counter)
    {
        const int key = mGlobalState.enableCounter(counter.getType(), counter.getEventCode());

        counter.setKey(key);
    }

    // Emits available counters
    int Driver::writeCounters(available_counter_consumer_t const & consumer) const
    {
        int count = 0;
        std::vector<std::string> counterNames = mGlobalState.getAllCounterNames();
        for (auto & counterName : counterNames) {
            consumer(counter_type_t::counter, counterName);
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
