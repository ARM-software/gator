/* Copyright (C) 2011-2023 by Arm Limited. All rights reserved. */

#include "StreamlineSetup.h"

#include "BufferUtils.h"
#include "CapturedXML.h"
#include "Configuration.h"
#include "ConfigurationXML.h"
#include "CounterXML.h"
#include "Driver.h"
#include "Drivers.h"
#include "ExitStatus.h"
#include "ICpuInfo.h"
#include "ISender.h"
#include "Logging.h"
#include "OlySocket.h"
#include "OlyUtility.h"
#include "Sender.h"
#include "SessionData.h"
#include "StreamlineSetupLoop.h"
#include "lib/Span.h"
#include "logging/suppliers.h"
#include "xml/CurrentConfigXML.h"
#include "xml/EventsXML.h"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <utility>

#include <mxml.h>
#include <unistd.h>

static const char TAG_SESSION[] = "session";
static const char TAG_REQUEST[] = "request";
static const char TAG_CONFIGURATIONS[] = "configurations";

static const char ATTR_TYPE[] = "type";
static const char VALUE_EVENTS[] = "events";
static const char VALUE_CONFIGURATION[] = "configuration";
static const char VALUE_COUNTERS[] = "counters";
static const char VALUE_CAPTURED[] = "captured";
static const char VALUE_DEFAULTS[] = "defaults";

StreamlineSetup::StreamlineSetup(OlySocket & s,
                                 Drivers & drivers,
                                 lib::Span<const CapturedSpe> capturedSpes,
                                 const logging::log_access_ops_t & log_ops)
    : mSocket(s), mDrivers(drivers), mCapturedSpes(capturedSpes), log_ops(log_ops)
{
    const auto result =
        streamlineSetupCommandLoop(s, *this, [](bool recvd) -> void { gSessionData.mWaitingOnCommand = !recvd; });

    if (result == State::EXIT_ERROR) {
        handleException();
    }
    else if (result == State::EXIT_OK) {
        //exit child and set status for gator main to exit
        exit(OK_TO_EXIT_GATOR_EXIT_CODE);
    }
    else if (result != State::EXIT_APC_START) {
        exit(0);
    }
}

IStreamlineCommandHandler::State StreamlineSetup::handleApcStart()
{
    LOG_FINE("Received apc start request");
    return State::EXIT_APC_START;
}

IStreamlineCommandHandler::State StreamlineSetup::handleApcStop()
{
    LOG_FINE("Received apc stop request before apc start request");
    return State::EXIT_APC_STOP;
}

IStreamlineCommandHandler::State StreamlineSetup::handleDisconnect()
{
    LOG_FINE("Received disconnect command");
    return State::EXIT_DISCONNECT;
}

IStreamlineCommandHandler::State StreamlineSetup::handlePing()
{
    LOG_FINE("Received ping command");
    sendData(nullptr, 0, ResponseType::ACK);
    return State::PROCESS_COMMANDS;
}

IStreamlineCommandHandler::State StreamlineSetup::handleExit()
{
    LOG_FINE("Received exit command");
    return State::EXIT_OK;
}

IStreamlineCommandHandler::State StreamlineSetup::handleRequest(char * xml)
{
    const char * attr = nullptr;

    auto * const tree = mxmlLoadString(nullptr, xml, MXML_NO_CALLBACK);
    auto * const node = mxmlFindElement(tree, tree, TAG_REQUEST, ATTR_TYPE, nullptr, MXML_DESCEND_FIRST);
    if (node != nullptr) {
        attr = mxmlElementGetAttr(node, ATTR_TYPE);
    }
    if ((attr != nullptr) && strcmp(attr, VALUE_EVENTS) == 0) {
        const auto xml = events_xml::getDynamicXML(mDrivers.getAllConst(),
                                                   mDrivers.getPrimarySourceProvider().getCpuInfo().getClusters(),
                                                   mDrivers.getPrimarySourceProvider().getDetectedUncorePmus());
        sendString(xml.get(), ResponseType::XML);
        LOG_FINE("Sent events xml response");
    }
    else if ((attr != nullptr) && strcmp(attr, VALUE_CONFIGURATION) == 0) {
        const auto & xml =
            configuration_xml::getConfigurationXML(mDrivers.getPrimarySourceProvider().getCpuInfo().getClusters());
        sendString(xml.raw.get(), ResponseType::XML);
        LOG_FINE("Sent configuration xml response");
    }
    else if ((attr != nullptr) && strcmp(attr, VALUE_COUNTERS) == 0) {
        const auto xml = counters_xml::getXML(mDrivers.getPrimarySourceProvider().supportsMultiEbs(),
                                              mDrivers.getAllConst(),
                                              mDrivers.getPrimarySourceProvider().getCpuInfo(),
                                              log_ops);
        sendString(xml.get(), ResponseType::XML);
        LOG_FINE("Sent counters xml response");
    }
    else if ((attr != nullptr) && strcmp(attr, VALUE_CAPTURED) == 0) {
        //Grab our template from the configuration xml
        const auto & templateConfiguration =
            configuration_xml::getConfigurationXML(mDrivers.getPrimarySourceProvider().getCpuInfo().getClusters())
                .templateConfiguration;

        const auto xml = captured_xml::getXML(false,
                                              mCapturedSpes,
                                              templateConfiguration,
                                              mDrivers.getPrimarySourceProvider(),
                                              mDrivers.getMaliHwCntrs().getDeviceGpuIds());
        sendString(xml.get(), ResponseType::XML);
        LOG_FINE("Sent captured xml response");
    }
    else if ((attr != nullptr) && strcmp(attr, VALUE_DEFAULTS) == 0) {
        sendDefaults();
        LOG_FINE("Sent default configuration xml response");
    }
    else {
        char error[] = "Unknown request";
        sendData(error, strlen(error), ResponseType::NAK);
        LOG_WARNING("Received unknown request:\n%s", xml);
    }

    mxmlDelete(tree);

    return State::PROCESS_COMMANDS;
}

IStreamlineCommandHandler::State StreamlineSetup::handleDeliver(char * xml)
{
    mxml_node_t * tree;

    // Determine xml type
    tree = mxmlLoadString(nullptr, xml, MXML_NO_CALLBACK);
    if (mxmlFindElement(tree, tree, TAG_SESSION, nullptr, nullptr, MXML_DESCEND_FIRST) != nullptr) {
        // Session XML
        gSessionData.parseSessionXML(xml);
        sendData(nullptr, 0, ResponseType::ACK);
        LOG_FINE("Received session xml");
    }
    else if (mxmlFindElement(tree, tree, TAG_CONFIGURATIONS, nullptr, nullptr, MXML_DESCEND_FIRST) != nullptr) {
        // Configuration XML
        writeConfiguration(xml);
        sendData(nullptr, 0, ResponseType::ACK);
        LOG_FINE("Received configuration xml");
    }
    else {
        // Unknown XML
        LOG_WARNING("Received unknown XML delivery type");
        sendData(nullptr, 0, ResponseType::NAK);
    }

    mxmlDelete(tree);

    return State::PROCESS_COMMANDS;
}

IStreamlineCommandHandler::State StreamlineSetup::handleRequestCurrentConfig()
{
    // Use ppid because StreamlineSetup is part of gator-child, need gator-main pid
    auto currentConfigXML = current_config_xml::generateCurrentConfigXML(getppid(),
                                                                         getuid(),
                                                                         gSessionData.mSystemWide,
                                                                         gSessionData.mWaitingOnCommand,
                                                                         gSessionData.mWaitForProcessCommand,
                                                                         gSessionData.mCaptureWorkingDir,
                                                                         gSessionData.mPids);
    sendString(currentConfigXML, ResponseType::CURRENT_CONFIG);
    return State::PROCESS_COMMANDS;
}

void StreamlineSetup::sendData(const char * data, uint32_t length, ResponseType type)
{
    char header[5];
    header[0] = static_cast<char>(type);
    buffer_utils::writeLEInt(header + 1, length);
    mSocket.send(header, sizeof(header));
    mSocket.send(data, length);
}

void StreamlineSetup::sendDefaults()
{
    // Send the config built into the binary
    auto xml =
        configuration_xml::getDefaultConfigurationXml(mDrivers.getPrimarySourceProvider().getCpuInfo().getClusters());
    size_t size = strlen(xml.get());

    // Artificial size restriction
    if (size > 1024 * 1024) {
        LOG_ERROR("Corrupt default configuration file");
        handleException();
    }

    sendData(xml.get(), size, ResponseType::XML);
}

void StreamlineSetup::writeConfiguration(char * xml)
{
    char path[PATH_MAX];

    configuration_xml::getPath(path, sizeof(path));

    if (writeToDisk(path, xml) < 0) {
        LOG_ERROR("Error writing %s\nPlease verify write permissions to this path.", path);
        handleException();
    }

    // Re-populate gSessionData with the configuration, as it has now changed
    auto checkError = [](const std::string & error) {
        if (!error.empty()) {
            LOG_ERROR("%s", error.c_str());
            handleException();
        }
    };

    auto && result =
        configuration_xml::getConfigurationXML(mDrivers.getPrimarySourceProvider().getCpuInfo().getClusters());
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
