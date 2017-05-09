/**
 * Copyright (C) ARM Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "ConfigurationXML.h"

#include <string.h>
#include <stdlib.h>
#include <dirent.h>

#include "Driver.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"

static const char TAG_CONFIGURATION[] = "configuration";

static const char ATTR_COUNTER[] = "counter";
static const char ATTR_REVISION[] = "revision";
static const char ATTR_EVENT[] = "event";
static const char ATTR_COUNT[] = "count";
static const char ATTR_CORES[] = "cores";

static const char CLUSTER_VAR[] = "${cluster}";

ConfigurationXML::ConfigurationXML()
        : mConfigurationXML(nullptr),
          mIndex(0)
{
    if (gSessionData.mCountersError != NULL) {
        free(gSessionData.mCountersError);
        gSessionData.mCountersError = NULL;
    }

    char path[PATH_MAX];

    getPath(path);
    mConfigurationXML = readFromDisk(path);

    for (int retryCount = 0; retryCount < 2; ++retryCount) {
        if (mConfigurationXML == NULL) {
            logg.logMessage("Unable to locate configuration.xml, using default in binary");
            mConfigurationXML = getDefaultConfigurationXml();
        }

        int ret = parse(mConfigurationXML);
        if (ret == 1) {
            remove();

            // Free the current configuration and reload
            free(mConfigurationXML);
            mConfigurationXML = NULL;
            continue;
        }

        break;
    }

    validate();
}

ConfigurationXML::~ConfigurationXML()
{
    if (mConfigurationXML) {
        free(mConfigurationXML);
    }
}

int ConfigurationXML::parse(const char* configurationXML)
{
    mxml_node_t *tree, *node;
    int ret;

    gSessionData.mIsEBS = false;
    mIndex = 0;

    // disable all counters prior to parsing the configuration xml
    for (int i = 0; i < MAX_PERFORMANCE_COUNTERS; i++) {
        gSessionData.mCounters[i].setEnabled(false);
    }

    tree = mxmlLoadString(NULL, configurationXML, MXML_NO_CALLBACK);

    node = mxmlGetFirstChild(tree);
    while (node && mxmlGetType(node) != MXML_ELEMENT)
        node = mxmlWalkNext(node, tree, MXML_NO_DESCEND);

    ret = configurationsTag(node);

    node = mxmlGetFirstChild(node);
    while (node) {
        if (mxmlGetType(node) != MXML_ELEMENT) {
            node = mxmlWalkNext(node, tree, MXML_NO_DESCEND);
            continue;
        }
        configurationTag(node);
        node = mxmlWalkNext(node, tree, MXML_NO_DESCEND);
    }

    mxmlDelete(tree);

    if (gSessionData.mCountersError == NULL && mIndex > MAX_PERFORMANCE_COUNTERS) {
        if (asprintf(&gSessionData.mCountersError, "Only %i performance counters are permitted, %i are selected",
        MAX_PERFORMANCE_COUNTERS,
                     mIndex) <= 0) {
            logg.logError("asprintf failed");
            handleException();
        }
    }
    gSessionData.mCcnDriver.validateCounters();

    return ret;
}

void ConfigurationXML::validate(void)
{
    for (int i = 0; i < MAX_PERFORMANCE_COUNTERS; i++) {
        const Counter & counter = gSessionData.mCounters[i];
        if (counter.isEnabled()) {
            if (strcmp(counter.getType(), "") == 0) {
                if (gSessionData.mCountersError == NULL
                        && asprintf(&gSessionData.mCountersError,
                                    "Invalid required attribute in configuration.xml:\n  counter=\"%s\"\n  event=%d",
                                    counter.getType(), counter.getEvent()) <= 0) {
                    logg.logError("asprintf failed");
                    handleException();
                }
                return;
            }

            // iterate through the remaining enabled performance counters
            for (int j = i + 1; j < MAX_PERFORMANCE_COUNTERS; j++) {
                const Counter & counter2 = gSessionData.mCounters[j];
                if (counter2.isEnabled()) {
                    // check if the types are the same
                    if (strcmp(counter.getType(), counter2.getType()) == 0) {
                        if (gSessionData.mCountersError == NULL
                                && asprintf(&gSessionData.mCountersError,
                                            "Duplicate performance counter type in configuration.xml: %s",
                                            counter.getType()) <= 0) {
                            logg.logError("asprintf failed");
                            handleException();
                        }
                        return;
                    }
                }
            }
        }
    }
}

#define CONFIGURATION_REVISION 3
int ConfigurationXML::configurationsTag(mxml_node_t *node)
{
    const char* revision_string;

    revision_string = mxmlElementGetAttr(node, ATTR_REVISION);
    if (!revision_string) {
        return 1; //revision issue;
    }

    int revision;
    if (!stringToInt(&revision, revision_string, 10)) {
        logg.logError("Configuration XML revision must be an integer");
        handleException();
    }
    if (revision < CONFIGURATION_REVISION) {
        return 1; // revision issue
    }

    // A revision >= CONFIGURATION_REVISION is okay
    // Greater than can occur when Streamline is newer than gator

    return 0;
}

void ConfigurationXML::configurationTag(mxml_node_t *node)
{
    // handle all other performance counters
    if (mIndex >= MAX_PERFORMANCE_COUNTERS) {
        mIndex++;
        return;
    }

    // read attributes
    Counter & counter = gSessionData.mCounters[mIndex];
    counter.clear();
    if (mxmlElementGetAttr(node, ATTR_COUNTER))
        counter.setType(mxmlElementGetAttr(node, ATTR_COUNTER));
    if (mxmlElementGetAttr(node, ATTR_EVENT)) {
        int event;
        if (!stringToInt(&event, mxmlElementGetAttr(node, ATTR_EVENT), 16)) {
            logg.logError("Configuration XML event must be an integer");
            handleException();
        }
        counter.setEvent(event);
    }
    if (mxmlElementGetAttr(node, ATTR_COUNT)) {
        int count;
        if (!stringToInt(&count, mxmlElementGetAttr(node, ATTR_COUNT), 10)) {
            logg.logError("Configuration XML count must be an integer");
            handleException();
        }
        counter.setCount(count);
    }
    if (mxmlElementGetAttr(node, ATTR_CORES)) {
        int cores;
        if (!stringToInt(&cores, mxmlElementGetAttr(node, ATTR_CORES), 10)) {
            logg.logError("Configuration XML cores must be an integer");
            handleException();
        }
        counter.setCores(cores);
    }
    if (counter.getCount() > 0) {
        gSessionData.mIsEBS = true;
    }
    counter.setEnabled(true);

    // Associate a driver with each counter
    for (Driver *driver = Driver::getHead(); driver != NULL; driver = driver->getNext()) {
        if (driver->claimCounter(counter)) {
            if (counter.getDriver() != NULL) {
                logg.logError("More than one driver has claimed %s:%i", counter.getType(), counter.getEvent());
                handleException();
            }
            counter.setDriver(driver);
        }
    }

    // If no driver is associated with the counter, disable it
    if (counter.getDriver() == NULL) {
        logg.logMessage("No driver has claimed %s:%i", counter.getType(), counter.getEvent());
        counter.setEnabled(false);
    }

    if (counter.isEnabled()) {
        // update counter index
        mIndex++;
    }
}

char *ConfigurationXML::getDefaultConfigurationXml()
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
            for (int cluster = 0; cluster < gSessionData.mSharedData->mClusterCount; ++cluster) {
                mxml_node_t *n = mxmlNewElement(mxmlGetParent(node), TAG_CONFIGURATION);
                copyMxmlElementAttrs(n, node);
                char buf[1 << 7];
                snprintf(buf, sizeof(buf), "%s%s", gSessionData.mSharedData->mClusters[cluster]->getPmncName(),
                         counter + sizeof(CLUSTER_VAR) - 1);
                mxmlElementSetAttr(n, ATTR_COUNTER, buf);
            }
            mxmlDelete(node);
        }
    }

    char *str = mxmlSaveAllocString(xml, mxmlWhitespaceCB);
    mxmlDelete(xml);
    return str;
}

void ConfigurationXML::getPath(char* path)
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

void ConfigurationXML::remove()
{
    char path[PATH_MAX];
    getPath(path);

    if (::remove(path) != 0) {
        logg.logError("Invalid configuration.xml file detected and unable to delete it. To resolve, delete configuration.xml on disk");
        handleException();
    }
    logg.logMessage("Invalid configuration.xml file detected and removed");
}
