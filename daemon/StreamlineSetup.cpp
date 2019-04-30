/**
 * Copyright (C) Arm Limited 2011-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "StreamlineSetup.h"

#include "BufferUtils.h"
#include "CapturedXML.h"
#include "ConfigurationXML.h"
#include "CounterXML.h"
#include "Driver.h"
#include "Drivers.h"
#include "EventsXML.h"
#include "ICpuInfo.h"
#include "Logging.h"
#include "OlySocket.h"
#include "OlyUtility.h"
#include "Sender.h"
#include "SessionData.h"

static const char TAG_SESSION[] = "session";
static const char TAG_REQUEST[] = "request";
static const char TAG_CONFIGURATIONS[] = "configurations";

static const char ATTR_TYPE[] = "type";
static const char VALUE_EVENTS[] = "events";
static const char VALUE_CONFIGURATION[] = "configuration";
static const char VALUE_COUNTERS[] = "counters";
static const char VALUE_CAPTURED[] = "captured";
static const char VALUE_DEFAULTS[] = "defaults";

StreamlineSetup::StreamlineSetup(OlySocket* s, Drivers & drivers, lib::Span<const CapturedSpe> capturedSpes)
        : mSocket(s), mDrivers(drivers), mCapturedSpes(capturedSpes)
{
    bool ready = false;

    // Receive commands from Streamline (master)
    while (!ready) {
        // receive command over socket
        gSessionData.mWaitingOnCommand = true;
        int type;
        auto data = readCommand(&type);

        // parse and handle data
        switch (type) {
            case COMMAND_REQUEST_XML:
                handleRequest(data.data());
                break;
            case COMMAND_DELIVER_XML:
                handleDeliver(data.data());
                break;
            case COMMAND_APC_START:
                logg.logMessage("Received apc start request");
                ready = true;
                break;
            case COMMAND_APC_STOP:
                logg.logMessage("Received apc stop request before apc start request");
                exit(0);
                break;
            case COMMAND_DISCONNECT:
                logg.logMessage("Received disconnect command");
                exit(0);
                break;
            case COMMAND_PING:
                logg.logMessage("Received ping command");
                sendData(NULL, 0, ResponseType::ACK);
                break;
            default:
                logg.logError("Target error: Unknown command type, %d", type);
                handleException();
        }
    }
}

StreamlineSetup::~StreamlineSetup()
{
}

std::vector<char> StreamlineSetup::readCommand(int* command)
{
    unsigned char header[5];
    int response;

    // receive type and length
    response = mSocket->receiveNBytes(reinterpret_cast<char *>(&header), sizeof(header));

    // After receiving a single byte, we are no longer waiting on a command
    gSessionData.mWaitingOnCommand = false;

    if (response < 0) {
        logg.logError("Target error: Unexpected socket disconnect");
        handleException();
    }

    const char type = header[0];
    const int length = (header[1] << 0) | (header[2] << 8) | (header[3] << 16) | (header[4] << 24);

    // add artificial limit
    if ((length < 0) || length > 1024 * 1024) {
        logg.logError("Target error: Invalid length received, %d", length);
        handleException();
    }

    // allocate memory to contain the xml file, size of zero returns a zero size object
    std::vector<char> data (length + 1);

    // receive data
    response = mSocket->receiveNBytes(data.data(), length);
    if (response < 0) {
        logg.logError("Target error: Unexpected socket disconnect");
        handleException();
    }

    // null terminate the data for string parsing
    if (length > 0) {
        data[length] = 0;
    }

    *command = type;
    return data;
}

void StreamlineSetup::handleRequest(char* xml)
{
    mxml_node_t *tree, *node;
    const char * attr = NULL;

    tree = mxmlLoadString(NULL, xml, MXML_NO_CALLBACK);
    node = mxmlFindElement(tree, tree, TAG_REQUEST, ATTR_TYPE, NULL, MXML_DESCEND_FIRST);
    if (node) {
        attr = mxmlElementGetAttr(node, ATTR_TYPE);
    }
    if (attr && strcmp(attr, VALUE_EVENTS) == 0) {
        const auto xml = events_xml::getXML(mDrivers.getAllConst(), mDrivers.getPrimarySourceProvider().getCpuInfo().getClusters());
        sendString(xml.get(), ResponseType::XML);
        logg.logMessage("Sent events xml response");
    }
    else if (attr && strcmp(attr, VALUE_CONFIGURATION) == 0) {
        const auto & xml = configuration_xml::getConfigurationXML(mDrivers.getPrimarySourceProvider().getCpuInfo().getClusters());
        sendString(xml.raw.get(), ResponseType::XML);
        logg.logMessage("Sent configuration xml response");
    }
    else if (attr && strcmp(attr, VALUE_COUNTERS) == 0) {
        const auto xml = counters_xml::getXML(mDrivers.getAllConst(), mDrivers.getPrimarySourceProvider().getCpuInfo());
        sendString(xml.get(), ResponseType::XML);
        logg.logMessage("Sent counters xml response");
    }
    else if (attr && strcmp(attr, VALUE_CAPTURED) == 0) {
        const auto xml = captured_xml::getXML(false, mCapturedSpes, mDrivers.getPrimarySourceProvider(), mDrivers.getMaliHwCntrs().getDeviceGpuIds());
        sendString(xml.get(), ResponseType::XML);
        logg.logMessage("Sent captured xml response");
    }
    else if (attr && strcmp(attr, VALUE_DEFAULTS) == 0) {
        sendDefaults();
        logg.logMessage("Sent default configuration xml response");
    }
    else {
        char error[] = "Unknown request";
        sendData(error, strlen(error), ResponseType::NAK);
        logg.logMessage("Received unknown request:\n%s", xml);
    }

    mxmlDelete(tree);
}

void StreamlineSetup::handleDeliver(char* xml)
{
    mxml_node_t *tree;

    // Determine xml type
    tree = mxmlLoadString(NULL, xml, MXML_NO_CALLBACK);
    if (mxmlFindElement(tree, tree, TAG_SESSION, NULL, NULL, MXML_DESCEND_FIRST)) {
        // Session XML
        gSessionData.parseSessionXML(xml);
        sendData(NULL, 0, ResponseType::ACK);
        logg.logMessage("Received session xml");
    }
    else if (mxmlFindElement(tree, tree, TAG_CONFIGURATIONS, NULL, NULL, MXML_DESCEND_FIRST)) {
        // Configuration XML
        writeConfiguration(xml);
        sendData(NULL, 0, ResponseType::ACK);
        logg.logMessage("Received configuration xml");
    }
    else {
        // Unknown XML
        logg.logMessage("Received unknown XML delivery type");
        sendData(NULL, 0, ResponseType::NAK);
    }

    mxmlDelete(tree);
}

void StreamlineSetup::sendData(const char* data, uint32_t length, ResponseType type)
{
    char header[5];
    header[0] = static_cast<char>(type);
    buffer_utils::writeLEInt(header + 1, length);
    mSocket->send(header, sizeof(header));
    mSocket->send(data, length);
}

void StreamlineSetup::sendDefaults()
{
    // Send the config built into the binary
    auto xml = configuration_xml::getDefaultConfigurationXml(mDrivers.getPrimarySourceProvider().getCpuInfo().getClusters());
    size_t size = strlen(xml.get());

    // Artificial size restriction
    if (size > 1024 * 1024) {
        logg.logError("Corrupt default configuration file");
        handleException();
    }

    sendData(xml.get(), size, ResponseType::XML);
}

void StreamlineSetup::writeConfiguration(char* xml)
{
    char path[PATH_MAX];

    configuration_xml::getPath(path);

    if (writeToDisk(path, xml) < 0) {
        logg.logError("Error writing %s\nPlease verify write permissions to this path.", path);
        handleException();
    }

    // Re-populate gSessionData with the configuration, as it has now changed
    auto checkError = [] (const std::string & error) {
        if (!error.empty()) {
            logg.logError("%s", error.c_str());
            handleException();
        }
    };

    auto && result = configuration_xml::getConfigurationXML(mDrivers.getPrimarySourceProvider().getCpuInfo().getClusters());
    std::set<CounterConfiguration> counterConfigs;
    for (auto && counter : result.counterConfigurations) {
        checkError(configuration_xml::addCounterToSet(counterConfigs, std::move(counter)));
    }
    std::set<SpeConfiguration> speConfigs;
    for (auto && counter : result.speConfigurations) {
        checkError(configuration_xml::addSpeToSet(speConfigs, std::move(counter)));
    }

    checkError(configuration_xml::setCounters(counterConfigs, !result.isDefault, mDrivers));
}
