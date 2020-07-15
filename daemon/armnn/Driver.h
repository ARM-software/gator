/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */
#pragma once

#include "../Driver.h"
#include "DriverSourceIpc.h"
#include "GlobalState.h"
#include "Session.h"
#include "SessionStateTracker.h"
#include "SocketAcceptor.h"
#include "SocketIO.h"
#include "ThreadManagementServer.h"

#include <memory>

namespace armnn {
    class Driver : public ::Driver {
    public:
        Driver();

        // Returns true if this driver can manage the counter
        virtual bool claimCounter(Counter & counter) const override;

        // Clears and disables all counters/SPE
        virtual void resetCounters() override;

        // Enables and prepares the counter for capture
        virtual void setupCounter(Counter & counter) override;

        // Emits available counters
        virtual int writeCounters(mxml_node_t * root) const override;

        // Emits possible dynamically generated events/counters
        void writeEvents(mxml_node_t * const /*unused*/) const override;

        // Called before the gator-child process is forked
        void preChildFork() override { mDriverSourceIpc.prepareForFork(); }

        // Called in the parent after the gator-child process exits
        void postChildExitInParent() override { mDriverSourceIpc.onChildDeath(); }

        ICaptureController & getCaptureController() { return mDriverSourceIpc; }

    private:
        GlobalState mGlobalState;
        SocketIO mAcceptingSocket;
        DriverSourceIpc mDriverSourceIpc;

        SessionSupplier createSession = [&](std::unique_ptr<SocketIO> connection) {
            return Session::create(std::move(connection), mGlobalState, mDriverSourceIpc);
        };
        ThreadManagementServer mSessionManager;
    };

}
