/* Copyright (C) 2019-2024 by Arm Limited. All rights reserved. */
#pragma once

#include "Config.h"
#include "Driver.h"
#include "armnn/GlobalState.h"
#include "armnn/ISessionConsumer.h"
#include "armnn/Session.h"
#include "armnn/SessionStateTracker.h"
#include "armnn/SocketAcceptor.h"
#include "armnn/SocketIO.h"
#include "armnn/ThreadManagementServer.h"

#if CONFIG_ARMNN_AGENT
#include "armnn/AcceptedSocketQueue.h"
#include "armnn/DriverSourceWithAgent.h"
#include "armnn/ISocketIOConsumer.h"
#else
#include "armnn/DriverSourceIpc.h"
#endif

#include <memory>

namespace armnn {
    class Driver : public ::Driver {
    public:
        Driver();

        // Returns true if this driver can manage the counter
        bool claimCounter(Counter & counter) const override;

        // Clears and disables all counters/SPE
        void resetCounters() override;

        // Enables and prepares the counter for capture
        void setupCounter(Counter & counter) override;

        // Emits available counters
        int writeCounters(available_counter_consumer_t const & consumer) const override;

        // Emits possible dynamically generated events/counters
        void writeEvents(mxml_node_t * const /*unused*/) const override;

        // Called before the gator-child process is forked
        void preChildFork() override { mDriverSourceConn.prepareForFork(); }

        // Called in the parent immediately after the gator-child process is forked
        void postChildForkInParent() override { mDriverSourceConn.afterFork(); }

        // Called in the parent after the gator-child process exits
        void postChildExitInParent() override { mDriverSourceConn.onChildDeath(); }

        [[nodiscard]] ICaptureController & getCaptureController() { return mDriverSourceConn; }

        void startAcceptingThread() { mSessionManager.start(); }

#if CONFIG_ARMNN_AGENT
        [[nodiscard]] ISocketIOConsumer & getAcceptedSocketConsumer() { return mAcceptedSocketQueue; }
#endif

    private:
        std::uint32_t mSessionCount {0};

        SessionSupplier createSession = [&](std::unique_ptr<ISocketIO> connection) {
            const std::uint32_t uniqueSessionID = mSessionCount++;

            return Session::create(std::move(connection), mGlobalState, mDriverSourceConn, uniqueSessionID);
        };

        GlobalState mGlobalState;

#if !CONFIG_ARMNN_AGENT
        SocketIO mAcceptingSocket;
#else
        AcceptedSocketQueue mAcceptedSocketQueue;
#endif
        ThreadManagementServer mSessionManager;

#if !CONFIG_ARMNN_AGENT
        DriverSourceIpc mDriverSourceConn;
#else
        DriverSourceWithAgent mDriverSourceConn;
#endif
    };

}
