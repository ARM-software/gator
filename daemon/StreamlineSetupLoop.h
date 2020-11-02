/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include "OlySocket.h"

#include <functional>

/**
 * Interface for the object that is called by the setup loop as each command is received
 */
class IStreamlineCommandHandler {
public:
    /** Returned by each command handler to indicate what the processing loop should do next */
    enum class State {
        /** The loop should continue to process commands */
        PROCESS_COMMANDS,
        /** The loop should terminate in a disconnect state */
        EXIT_DISCONNECT,
        /** The loop should terminate in a no-capture state */
        EXIT_APC_STOP,
        /** The loop should terminate in a start-capture state */
        EXIT_APC_START,
        /** The loop terminated due to read failure */
        EXIT_ERROR
    };

    virtual ~IStreamlineCommandHandler() = default;

    virtual State handleRequest(char * xml) = 0;
    virtual State handleDeliver(char * xml) = 0;
    virtual State handleApcStart() = 0;
    virtual State handleApcStop() = 0;
    virtual State handleDisconnect() = 0;
    virtual State handlePing() = 0;
};

/**
 * Command handler loop function
 *
 * @param socket The socket
 * @param handler The command handler
 * @param receivedOneByteCallback The callback that is called once a single byte is received
 * @return One of the state value starting EXIT_xxxx indicating how the loop terminated
 */
IStreamlineCommandHandler::State streamlineSetupCommandLoop(OlySocket & socket,
                                                            IStreamlineCommandHandler & handler,
                                                            const std::function<void(bool)> & receivedOneByteCallback);

/**
 * Command handler loop single iteration
 *
 * @param socket The socket
 * @param handler The command handler
 * @param receivedOneByteCallback The callback that is called once a single byte is received
 * @return One of the state values
 */
IStreamlineCommandHandler::State streamlineSetupCommandIteration(
    OlySocket & socket,
    IStreamlineCommandHandler & handler,
    const std::function<void(bool)> & receivedOneByteCallback);
