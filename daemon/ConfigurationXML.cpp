/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "ConfigurationXML.h"

#include <string.h>
#include <stdlib.h>
#include <dirent.h>

#include <sstream>

#include "lib/Format.h"

#include "Drivers.h"
#include "ICpuInfo.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"
#include "Configuration.h"
#include "ConfigurationXMLParser.h"
#include "CCNDriver.h"

static const char TAG_CONFIGURATION[] = "configuration";

static const char ATTR_COUNTER[] = "counter";

static const char CLUSTER_VAR[] = "${cluster}";

#define CONFIGURATION_REVISION 3

static void appendError(std::ostream & error, const std::string & possibleError)
{
    if (!possibleError.empty()) {
        if (error.tellp() != 0) {
            error << "\n\n";
        }
        error << possibleError;
    }
}

namespace configuration_xml {

static bool addCounter(const char * counterName, int event, int count, int cores, int mIndex,
                       bool printWarningIfUnclaimed, lib::Span<Driver * const > drivers);

Contents getConfigurationXML(lib::Span<const GatorCpu> clusters)
{
    // try the configuration.xml file first
    {
        char path[PATH_MAX];
        getPath(path);
        std::unique_ptr<char, void (*)(void *)> configurationXML { readFromDisk(path), &free };

        if (configurationXML) {
            ConfigurationXMLParser parser;
            if (parser.parseConfigurationContent(configurationXML.get()) == 0) {
                return {std::move(configurationXML), false, parser.getCounterConfiguration(), parser.getSpeConfiguration()};
            }
            else {
                // invalid so delete it
                remove();
            }
        }
    }

    // fall back to the defaults
    logg.logMessage("Unable to locate configuration.xml, using default in binary");

    {
        auto && configurationXML = getDefaultConfigurationXml(clusters);
        ConfigurationXMLParser parser;
        if (parser.parseConfigurationContent(configurationXML.get()) == 0) {
            return {std::move(configurationXML), true, parser.getCounterConfiguration(), parser.getSpeConfiguration()};
        }
    }

    // should not happen
    logg.logError("bad default configuration.xml");
    handleException();
}

std::string addCounterToSet(std::set<CounterConfiguration> & configs, CounterConfiguration && config) {
    if (config.counterName.empty()) {
        return "A <counter> was found with an empty name";
    }

    const auto & insertion = configs.insert(std::move(config));
    if (!insertion.second) {
        return lib::Format() << "Duplicate <counter> found '" << insertion.first->counterName << "'";
    }

    return {};
}

std::string addSpeToSet(std::set<SpeConfiguration> & configs, SpeConfiguration && config) {
    if (config.id.empty()) {
        return "An <spe> was found with an empty id";
    }

    const auto & insertion = configs.insert(std::move(config));
    if (!insertion.second) {
        return lib::Format() << "Duplicate <spe> found \"" << insertion.first->id << "\"";
    }

    return {};
}

std::string setCounters(const std::set<CounterConfiguration> & counterConfigurations, bool printWarningIfUnclaimed, Drivers & drivers)
{
    gSessionData.mIsEBS = false;

    std::ostringstream error;

    // disable all counters prior to parsing the configuration xml
    for (int i = 0; i < MAX_PERFORMANCE_COUNTERS; i++) {
        gSessionData.mCounters[i].setEnabled(false);
    }

    //Add counter
    int index = 0;
    for (CounterConfiguration cc : counterConfigurations) {
        if (index >= MAX_PERFORMANCE_COUNTERS) {
            error << "Only " //
                    << MAX_PERFORMANCE_COUNTERS //
                    << " performance counters are permitted, " //
                    << counterConfigurations.size() //
                    << " are selected."; //
            break;
        }
        const bool added = addCounter(cc.counterName.c_str(), cc.event, cc.count, cc.cores, index,
                                printWarningIfUnclaimed, drivers.getAll());
        if (added) {
            // update counter index
            index++;
        }
    }

    appendError(error, drivers.getCcnDriver().validateCounters());

    return error.str();
}


std::unique_ptr<char, void (*)(void *)> getDefaultConfigurationXml(lib::Span<const GatorCpu> clusters)
{
#include "defaults_xml.h" // defines and initializes char defaults_xml[] and int defaults_xml_len
    (void) defaults_xml_len;

    // Resolve ${cluster}
    mxml_node_t *xml = mxmlLoadString(NULL, reinterpret_cast<const char *>(defaults_xml), MXML_NO_CALLBACK);
    for (mxml_node_t *node = mxmlFindElement(xml, xml, TAG_CONFIGURATION, NULL, NULL, MXML_DESCEND),
            *next = mxmlFindElement(node, xml, TAG_CONFIGURATION, NULL, NULL, MXML_DESCEND); node != NULL;
            node = next, next = mxmlFindElement(node, xml, TAG_CONFIGURATION, NULL, NULL, MXML_DESCEND)) {
        const char *counter = mxmlElementGetAttr(node, ATTR_COUNTER);
        if (counter != NULL && strncmp(counter, CLUSTER_VAR, sizeof(CLUSTER_VAR) - 1) == 0) {
            for (const GatorCpu & cluster : clusters) {
                mxml_node_t *n = mxmlNewElement(mxmlGetParent(node), TAG_CONFIGURATION);
                copyMxmlElementAttrs(n, node);
                char buf[1 << 7];
                snprintf(buf, sizeof(buf), "%s%s", cluster.getPmncName(),
                counter + sizeof(CLUSTER_VAR) - 1);
                mxmlElementSetAttr(n, ATTR_COUNTER, buf);
            }
            mxmlDelete(node);
        }
    }

    char *str = mxmlSaveAllocString(xml, mxmlWhitespaceCB);
    mxmlDelete(xml);
    return {str, &free};
}

void getPath(char* path)
{
    if (gSessionData.mConfigurationXMLPath) {
        strncpy(path, gSessionData.mConfigurationXMLPath, PATH_MAX);
    }
    else {
        if (getApplicationFullPath(path, PATH_MAX) != 0) {
            logg.logMessage("Unable to determine the full path of gatord, the cwd will be used");
        }
        strncat(path, "configuration.xml", PATH_MAX - strlen(path) - 1);
    }
}

void remove()
{
    char path[PATH_MAX];
    getPath(path);

    if (::remove(path) != 0) {
        logg.logError("Invalid configuration.xml file detected and unable to delete it. To resolve, delete configuration.xml on disk");
        handleException();
    }
    logg.logMessage("Invalid configuration.xml file detected and removed");
}

static bool addCounter(const char * counterName, int event, int count, int cores, int mIndex,
                       bool printWarningIfUnclaimed, lib::Span<Driver * const > drivers)
{

    const auto end = gSessionData.globalCounterToEventMap.end();
    const auto it = std::find_if(gSessionData.globalCounterToEventMap.begin(), end, [&counterName] (const std::pair<std::string, int> & pair) {
                return strcasecmp(pair.first.c_str(), counterName) == 0;
            });
    const bool hasEventsXmlCounter = (it != end);
    const int counterEvent = (hasEventsXmlCounter ? it->second : -1);
    // read attributes
    Counter & counter = gSessionData.mCounters[mIndex];
    counter.clear();
    counter.setType(counterName);

    // if hasEventsXmlCounter, then then event is defined as a counter with 'counter'/'type' attribute
    // in events.xml. Use the specified event from events.xml (which may be -1 if not relevant)
    // overriding anything from user map. This is necessary for cycle counters for example where
    // they have a name "XXX_ccnt" but also often an event code. If not the event code -1 is used
    // which is incorrect.
    if (hasEventsXmlCounter) {
        counter.setEvent(counterEvent);
    }
    // the counter is not in events.xml. This usually means it is a PMU slot counter
    // the user specified the event code, use that
    else if (event > -1) {
        counter.setEvent(event);
    }
    // the counter is not in events.xml. This usually means it is a PMU slot counter, but since
    // the user has not specified an event code, this is probably incorrect.
    else if (strcasestr(counterName, "_cnt")) {
        logg.logWarning("Counter '%s' does not have an event code specified, PMU slot counters require an event code", counterName);
    }
    else {
        logg.logWarning("Counter '%s' was not recognized", counterName);
    }
    counter.setCount(count);
    counter.setCores(cores);
    if (counter.getCount() > 0) {
        gSessionData.mIsEBS = true;
    }
    counter.setEnabled(true);
    // Associate a driver with each counter
    for (Driver *driver : drivers) {
        if (driver->claimCounter(counter)) {
            if ((counter.getDriver() != nullptr) && (counter.getDriver() != driver)) {
                logg.logError("More than one driver has claimed %s:%i (%s vs %s)", counter.getType(), counter.getEvent(), counter.getDriver()->getName(), driver->getName());
                handleException();
            }
            counter.setDriver(driver);
        }
    }
    // If no driver is associated with the counter, disable it
    if (counter.getDriver() == nullptr) {
        if (printWarningIfUnclaimed)
            logg.logWarning("No driver has claimed %s:%i", counter.getType(), counter.getEvent());
        counter.setEnabled(false);
    }
    return counter.isEnabled();
}
}
