/* Copyright (C) 2010-2025 by Arm Limited. All rights reserved. */

#include "ConfigurationXML.h"

#include "CCNDriver.h"
#include "Configuration.h"
#include "ConfigurationXMLParser.h"
#include "Counter.h"
#include "Drivers.h"
#include "EventCode.h"
#include "ICpuInfo.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"
#include "lib/Format.h"
#include "lib/Span.h"
#include "lib/String.h"
#include "libGPUInfo/source/libgpuinfo.hpp"
#include "xml/EventsXML.h"
#include "xml/EventsXMLHelpers.h"
#include "xml/MxmlUtils.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#include <mxml.h>

#define CONFIGURATION_REVISION 3

namespace {
    constexpr const char * TAG_CONFIGURATION = "configuration";
    constexpr const char * TAG_CONFIGURATIONS = "configurations";
    constexpr const char * ATTR_COUNTER = "counter";
    constexpr const char * ATTR_GPU_PUBLIC_NAME = "gpu_public_name";
    constexpr const char * CLUSTER_VAR = "${cluster}";

    void appendError(std::ostream & error, const std::string & possibleError)
    {
        if (!possibleError.empty()) {
            if (error.tellp() != 0) {
                error << "\n\n";
            }
            error << possibleError;
        }
    }
}

namespace configuration_xml {

    namespace {
#include "defaults_xml.h"
    }

    static void addCounter(const char * counterName,
                           const EventCode & event,
                           int count,
                           int cores,
                           bool printWarningIfUnclaimed,
                           lib::Span<Driver * const> drivers,
                           const std::map<std::string, EventCode> & counterToEventMap);

    static std::optional<std::string> getGpuPublicName()
    {
        LOG_FINE("Attempting to grab GPU info from libGPUInfo and pass to configuraton.xml");
        // Get the instance at id 0, as in multi-gpu case gpus are homogenous.
        const auto gpuInfoInstance = libgpuinfo::instance::create();

        if (gpuInfoInstance == nullptr) {
            return std::nullopt;
        }

        return {std::string(gpuInfoInstance->get_info().gpu_name)};
    }

    Contents getConfigurationXML(lib::Span<const GatorCpu> clusters)
    {
        // try the configuration.xml file first
        {
            char path[PATH_MAX];
            getPath(path, sizeof(path));
            std::unique_ptr<char, void (*)(void *)> configurationXML {readFromDisk(path), &free};

            if (configurationXML) {
                // Check existing config xml for gpu_public_name and update it.
                mxml_node_t * xmlDoc = mxmlLoadString(nullptr, configurationXML.get(), MXML_OPAQUE_CALLBACK);
                if (xmlDoc != nullptr) {
                    //Find <configurations> tag.
                    mxml_node_t * configurationsNode =
                        mxmlFindElement(xmlDoc, xmlDoc, TAG_CONFIGURATIONS, nullptr, nullptr, MXML_DESCEND);

                    if (configurationsNode != nullptr) {
                        //Get the gpu public name from libGPUInfo
                        std::string gpuPublicName = getGpuPublicName().value_or("");

                        if (!gpuPublicName.empty()) {
                            mxmlElementSetAttr(configurationsNode, ATTR_GPU_PUBLIC_NAME, gpuPublicName.c_str());
                        }
                    }

                    char * modifiedXml = mxmlSaveAllocString(xmlDoc, MXML_NO_CALLBACK);

                    configurationXML.reset(modifiedXml);

                    mxmlDelete(xmlDoc);
                }

                ConfigurationXMLParser parser;
                if (parser.parseConfigurationContent(configurationXML.get()) == 0) {
                    return {std::move(configurationXML),
                            false,
                            parser.getCounterConfiguration(),
                            parser.getSpeConfiguration(),
                            parser.getTemplateConfiguration()};
                }
                // invalid so delete it
                remove();
            }
        }

        // fall back to the defaults
        LOG_DEBUG("Unable to locate configuration.xml, using default in binary");

        {
            auto && configurationXML = getDefaultConfigurationXml(clusters);
            ConfigurationXMLParser parser;
            if (parser.parseConfigurationContent(configurationXML.get()) == 0) {
                return {std::move(configurationXML),
                        true,
                        parser.getCounterConfiguration(),
                        parser.getSpeConfiguration(),
                        parser.getTemplateConfiguration()};
            }
        }

        // should not happen
        LOG_ERROR("bad default configuration.xml");
        handleException();
    }

    std::string addCounterToSet(std::set<CounterConfiguration> & configs, CounterConfiguration && config)
    {
        if (config.counterName.empty()) {
            return "A <counter> was found with an empty name";
        }

        const auto & insertion = configs.insert(std::move(config));
        if (!insertion.second) {
            return lib::Format() << "Duplicate <counter> found '" << insertion.first->counterName << "'";
        }

        return {};
    }

    std::string addSpeToSet(std::set<SpeConfiguration> & configs, SpeConfiguration && config)
    {
        if (config.id.empty()) {
            return "An <spe> was found with an empty id";
        }

        const auto & insertion = configs.insert(std::move(config));
        if (!insertion.second) {
            return lib::Format() << "Duplicate <spe> found \"" << insertion.first->id << "\"";
        }

        return {};
    }

    std::string setCounters(const std::set<CounterConfiguration> & counterConfigurations,
                            bool printWarningIfUnclaimed,
                            Drivers & drivers)
    {
        gSessionData.mIsEBS = false;

        std::ostringstream error;

        // clear all previous counters prior to parsing the configuration xml
        gSessionData.mCounters.clear();

        const std::map<std::string, EventCode> counterToEventMap =
            events_xml::getCounterToEventMap(drivers.getAllConst(),
                                             drivers.getPrimarySourceProvider().getCpuInfo().getClusters(),
                                             drivers.getPrimarySourceProvider().getDetectedUncorePmus());
        //Add counters
        for (const CounterConfiguration & cc : counterConfigurations) {
            addCounter(cc.counterName.c_str(),
                       cc.event,
                       cc.count,
                       cc.cores,
                       printWarningIfUnclaimed,
                       drivers.getAll(),
                       counterToEventMap);
        }

        appendError(error, CCNDriver::validateCounters());

        return error.str();
    }

    std::unique_ptr<char, void (*)(void *)> getDefaultConfigurationXml(lib::Span<const GatorCpu> clusters)
    {
        constexpr const std::size_t BUFFER_SIZE = 128;

        mxml_node_t * xml = mxmlLoadString(nullptr, DEFAULTS_XML.data(), MXML_NO_CALLBACK);

        // Attempt to add the target gpu name, and add it to the config xml if we can.
        // We use this in the GUI to find the matching template for this GPU.
        // This allows us to distinguish between Mali/Immortalis variants.
        mxml_node_t * configurations_node =
            mxmlFindElement(xml, xml, TAG_CONFIGURATIONS, nullptr, nullptr, MXML_DESCEND);

        if (configurations_node != nullptr) {
            std::string gpuPublicName = getGpuPublicName().value_or("");

            if (!gpuPublicName.empty()) {
                mxmlElementSetAttr(configurations_node, ATTR_GPU_PUBLIC_NAME, gpuPublicName.c_str());
            }
        }

        // Resolve ${cluster}
        for (mxml_node_t *node = mxmlFindElement(xml, xml, TAG_CONFIGURATION, nullptr, nullptr, MXML_DESCEND),
                         *next = mxmlFindElement(node, xml, TAG_CONFIGURATION, nullptr, nullptr, MXML_DESCEND);
             node != nullptr;
             node = next, next = mxmlFindElement(node, xml, TAG_CONFIGURATION, nullptr, nullptr, MXML_DESCEND)) {
            const char * counter = mxmlElementGetAttr(node, ATTR_COUNTER);
            if (counter != nullptr && strncmp(counter, CLUSTER_VAR, strlen(CLUSTER_VAR)) == 0) {
                for (const GatorCpu & cluster : clusters) {
                    mxml_node_t * n = mxmlNewElement(mxmlGetParent(node), TAG_CONFIGURATION);
                    copyMxmlElementAttrs(n, node);
                    lib::printf_str_t<BUFFER_SIZE> buf {"%s%s", cluster.getId(), counter + strlen(CLUSTER_VAR)};
                    mxmlElementSetAttr(n, ATTR_COUNTER, buf);
                }
                mxmlDelete(node);
            }
        }

        char * str = mxmlSaveAllocString(xml, mxmlWhitespaceCB);
        mxmlDelete(xml);
        return {str, &free};
    }

    void getPath(char * path, size_t n)
    {
        if (gSessionData.mConfigurationXMLPath != nullptr) {
            strncpy(path, gSessionData.mConfigurationXMLPath, n - 1);
            path[n - 1] = '\0';
        }
        else {
            if (getApplicationFullPath(path, n) != 0) {
                LOG_DEBUG("Unable to determine the full path of gatord, the cwd will be used");
            }
            size_t available = (strlen(path) < n) ? (n - strlen(path) - 1) : 0;
            strncat(path, "configuration.xml", available);
        }
    }

    void remove()
    {
        char path[PATH_MAX];
        getPath(path, sizeof(path));

        if (::remove(path) != 0) {
            LOG_ERROR("Invalid configuration.xml file detected and unable to delete it. To resolve, delete "
                      "configuration.xml on disk");
            handleException();
        }
        LOG_FINE("Invalid configuration.xml file detected and removed");
    }

    static void addCounter(const char * counterName,
                           const EventCode & event,
                           int count,
                           int cores,
                           bool printWarningIfUnclaimed,
                           lib::Span<Driver * const> drivers,
                           const std::map<std::string, EventCode> & counterToEventMap)
    {

        const auto eventsXmlCounterEnd = counterToEventMap.end();
        const auto eventsXmlCounterIterator =
            std::find_if(counterToEventMap.begin(),
                         eventsXmlCounterEnd,
                         [&counterName](const std::pair<std::string, EventCode> & pair) {
                             return strcasecmp(pair.first.c_str(), counterName) == 0;
                         });

        // read attributes
        Counter & counter = gSessionData.mCounters.emplace_back();
        counter.clear();
        counter.setType(counterName);

        // if hasEventsXmlCounter, then then event is defined as a counter with 'counter'/'type' attribute
        // in events.xml. Use the specified event from events.xml (which may be -1 if not relevant)
        // overriding anything from user map. This is necessary for cycle counters for example where
        // they have a name "XXX_ccnt" but also often an event code. If not the event code -1 is used
        // which is incorrect.
        if (eventsXmlCounterIterator != eventsXmlCounterEnd) {
            if (eventsXmlCounterIterator->second.isValid()) {
                counter.setEventCode(eventsXmlCounterIterator->second);
            }
        }
        // the counter is not in events.xml. This usually means it is a PMU slot counter
        // the user specified the event code, use that
        else if (event.isValid()) {
            counter.setEventCode(event);
        }
        // the counter is not in events.xml. This usually means it is a PMU slot counter, but since
        // the user has not specified an event code, this is probably incorrect.
        else if (printWarningIfUnclaimed) {
            if (strcasestr(counterName, "_cnt") != nullptr) {
                LOG_WARNING(
                    "Counter \"%s\" does not have an event code specified, PMU slot counters require an event code",
                    counterName);
            }
            else {
                LOG_WARNING("Counter \"%s\" was not recognized", counterName);
            }
        }
        counter.setCount(count);
        counter.setCores(cores);
        if (counter.getCount() > 0) {
            gSessionData.mIsEBS = true;
        }
        counter.setEnabled(true);
        // Associate a driver with each counter
        for (Driver * driver : drivers) {
            if (driver->claimCounter(counter)) {
                if ((counter.getDriver() != nullptr) && (counter.getDriver() != driver)) {
                    const auto & optionalEventCode = counter.getEventCode();
                    LOG_ERROR("More than one driver has claimed %s:0x%" PRIxEventCode " (%s vs %s)",
                              counter.getType(),
                              (optionalEventCode.isValid() ? optionalEventCode.asU64() : 0),
                              counter.getDriver()->getName(),
                              driver->getName());
                    handleException();
                }
                counter.setDriver(driver);
            }
        }
        // If no driver is associated with the counter, disable it
        if (counter.getDriver() == nullptr) {
            if (printWarningIfUnclaimed) {
                const auto & optionalEventCode = counter.getEventCode();

                LOG_DEBUG("No driver has claimed %s:0x%" PRIxEventCode,
                          counter.getType(),
                          (optionalEventCode.isValid() ? optionalEventCode.asU64() : 0));
            }
            counter.setEnabled(false);
        }

        // remove it if it is not enabled
        if (!counter.isEnabled()) {
            gSessionData.mCounters.pop_back();
        }
    }
}
