/* Copyright (C) 2020-2024 by Arm Limited. All rights reserved. */

#include "StreamlineSetupLoop.h"

#include "Logging.h"
#include "OlySocket.h"

#include <functional>
#include <vector>

namespace {
    // Commands from Streamline
    enum {
        COMMAND_ERROR = -1,
        COMMAND_REQUEST_XML = 0,
        COMMAND_DELIVER_XML = 1,
        COMMAND_APC_START = 2,
        COMMAND_APC_STOP = 3,
        COMMAND_DISCONNECT = 4,
        COMMAND_PING = 5,
        COMMAND_EXIT = 6,
        // A request to get gatord configuration (in XML format)
        // Not to be confused with configuration.xml
        COMMAND_REQUEST_CURRENT_CONFIG = 7
    };

    struct ReadResult {
        int commandType;
        std::vector<uint8_t> data;
    };

    ReadResult readCommand(OlySocket & socket, const std::function<void(bool)> & receivedOneByteCallback)
    {
        ReadResult result {COMMAND_ERROR, {}};

        uint8_t header[5];
        int response;

        // receive type and length
        response = socket.receiveNBytes(header, sizeof(header));

        // After receiving a single byte, we are no longer waiting on a command
        receivedOneByteCallback(true);

        if (response < 0) {
            LOG_ERROR("Target error: Unexpected socket disconnect");
            return result;
        }

        const auto type = header[0];
        const auto length = (header[1] << 0) | (header[2] << 8) | (header[3] << 16) | (header[4] << 24);

        // add artificial limit
        if ((length < 0) || length > 1024 * 1024) {
            LOG_ERROR("Target error: Invalid length received, %d", length);
            return result;
        }

        // allocate data for receive
        result.data.resize(length + 1, 0);

        // receive data
        response = socket.receiveNBytes(result.data.data(), length);
        if (response < 0) {
            LOG_ERROR("Target error: Unexpected socket disconnect");
            return result;
        }

        // null terminate the data for string parsing
        if (length > 0) {
            result.data[length] = 0;
        }

        // set type to non-error value
        result.commandType = type;

        return result;
    }
}

IStreamlineCommandHandler::State streamlineSetupCommandLoop(OlySocket & socket,
                                                            IStreamlineCommandHandler & handler,
                                                            const std::function<void(bool)> & receivedOneByteCallback)
{
    // Receive commands from Streamline (master)
    IStreamlineCommandHandler::State currentState = IStreamlineCommandHandler::State::PROCESS_COMMANDS;
    while (currentState == IStreamlineCommandHandler::State::PROCESS_COMMANDS) {
        currentState = streamlineSetupCommandIteration(socket, handler, receivedOneByteCallback);
    }
    return currentState;
}

IStreamlineCommandHandler::State streamlineSetupCommandIteration(
    OlySocket & socket,
    IStreamlineCommandHandler & handler,
    const std::function<void(bool)> & receivedOneByteCallback)
{
    // waiting for some byte
    receivedOneByteCallback(false);

    // receive command over socket
    auto readResult = readCommand(socket, receivedOneByteCallback);

    // parse and handle data
    switch (readResult.commandType) {
        case COMMAND_ERROR:
            return IStreamlineCommandHandler::State::EXIT_ERROR;
        case COMMAND_REQUEST_XML:
            return handler.handleRequest(reinterpret_cast<char *>(readResult.data.data()));
        case COMMAND_DELIVER_XML:
            return handler.handleDeliver(reinterpret_cast<char *>(readResult.data.data()));
        case COMMAND_APC_START:
            if (!readResult.data.empty()) {
                LOG_DEBUG("INVESTIGATE: Received APC_START command but with length = %zu", readResult.data.size());
            }
            return handler.handleApcStart();
        case COMMAND_APC_STOP:
            if (!readResult.data.empty()) {
                LOG_DEBUG("INVESTIGATE: Received APC_STOP command but with length = %zu", readResult.data.size());
            }
            return handler.handleApcStop();
        case COMMAND_DISCONNECT:
            if (!readResult.data.empty()) {
                LOG_DEBUG("INVESTIGATE: Received DISCONNECT command but with length = %zu", readResult.data.size());
            }
            return handler.handleDisconnect();
        case COMMAND_PING:
            if (!readResult.data.empty()) {
                LOG_DEBUG("INVESTIGATE: Received PING command but with length = %zu", readResult.data.size());
            }
            return handler.handlePing();
        case COMMAND_EXIT:
            //No logging on length needed as there will be no additional data
            return handler.handleExit();
        case COMMAND_REQUEST_CURRENT_CONFIG:
            if (!readResult.data.empty()) {
                LOG_DEBUG("INVESTIGATE: Received REQUEST_CONFIG command but with length = %zu", readResult.data.size());
            }
            return handler.handleRequestCurrentConfig();
        default:
            LOG_ERROR("Target error: Unknown command type, %d", readResult.commandType);
            return IStreamlineCommandHandler::State::EXIT_ERROR;
    }
}
